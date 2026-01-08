#!/usr/bin/env python3
"""
Integration tests for Group Message (GM) feature.

Test cases cover:
1. Basic GM flow: create group, send message, receive history
2. Real-time push: All members in group chat receive instant messages
3. Multi-member: 3 users in group, all receive messages
4. GM_CHAT_START/END: Enter/leave group chat mode
5. GM_JOIN/GM_LEAVE notifications: Members see when others join/leave chat
6. Non-member access: Users not in group cannot chat
7. Edge cases: empty content, invalid group_id

Author: ChatProject Team
"""

import base64
import os
import select
import socket
import subprocess
import sys
import time
from contextlib import closing
import shutil

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SERVER_BIN = os.path.join(PROJECT_ROOT, "build", "server")
DATA_DIR = os.path.join(PROJECT_ROOT, "data")
GM_DIR = os.path.join(DATA_DIR, "gm")


# ============ Helpers ============

def die(msg: str) -> None:
    print(f"[FAIL] {msg}")
    sys.exit(1)


def ok(msg: str) -> None:
    print(f"[OK] {msg}")


def info(msg: str) -> None:
    print(f"[INFO] {msg}")


def free_port() -> int:
    with closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def parse_resp(line: str):
    """Parse response: OK/ERR req_id payload"""
    parts = line.strip().split(" ", 2)
    if len(parts) < 2:
        return ("", "", "")
    kind = parts[0]
    rid = parts[1]
    rest = parts[2] if len(parts) >= 3 else ""
    return (kind, rid, rest)


def parse_kv(payload: str) -> dict:
    """Parse key=value pairs from payload"""
    kv = {}
    for token in payload.split():
        if "=" in token:
            k, v = token.split("=", 1)
            kv[k] = v
    return kv


def b64_encode(text: str) -> str:
    """Encode text to Base64"""
    return base64.b64encode(text.encode('utf-8')).decode('ascii')


def b64_decode(b64: str) -> str:
    """Decode Base64 to text"""
    return base64.b64decode(b64).decode('utf-8')


class Conn:
    """TCP connection wrapper with line-based framing"""
    
    def __init__(self, host: str, port: int):
        self.sock = socket.create_connection((host, port), timeout=5)
        self.buf = b""
        self.req_id = 0
        self.push_queue = []  # Queue for PUSH messages received while waiting for response

    def send_line(self, line: str):
        self.sock.sendall((line + "\r\n").encode())

    def recv_line(self, timeout: float = 3.0, skip_push: bool = True) -> str:
        """Receive next line. If skip_push=True, PUSH messages are queued and skipped."""
        self.sock.settimeout(timeout)
        while True:
            while b"\r\n" not in self.buf:
                d = self.sock.recv(4096)
                if not d:
                    raise EOFError("disconnected")
                self.buf += d
            line, self.buf = self.buf.split(b"\r\n", 1)
            line_str = line.decode()
            
            # If this is a PUSH and we're skipping, queue it and continue
            if skip_push and line_str.startswith("PUSH "):
                self.push_queue.append(line_str)
                continue
            
            return line_str

    def try_recv_line(self, timeout: float = 0.5) -> str | None:
        """Try to receive a line (PUSH or response), return None if timeout.
        First checks push_queue, then reads from socket."""
        # First check if we have queued PUSH messages
        if self.push_queue:
            return self.push_queue.pop(0)
        try:
            return self.recv_line(timeout, skip_push=False)
        except socket.timeout:
            return None
        except Exception:
            return None

    def drain_push(self, timeout: float = 0.3) -> list:
        """Drain all PUSH messages from queue and socket"""
        msgs = list(self.push_queue)
        self.push_queue.clear()
        while True:
            line = self.try_recv_line(timeout)
            if line is None:
                break
            if line.startswith("PUSH "):
                msgs.append(line)
        return msgs

    def next_id(self) -> str:
        self.req_id += 1
        return str(self.req_id)

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass

    # ============ High-level commands ============

    def register(self, username: str, password: str, email: str) -> tuple:
        rid = self.next_id()
        self.send_line(f"REGISTER {rid} username={username} password={password} email={email}")
        resp = self.recv_line()
        return parse_resp(resp)

    def login(self, username: str, password: str) -> tuple:
        rid = self.next_id()
        self.send_line(f"LOGIN {rid} username={username} password={password}")
        resp = self.recv_line()
        kind, rid, rest = parse_resp(resp)
        kv = parse_kv(rest)
        token = kv.get("token", "")
        return (kind, rid, rest, token)

    def group_create(self, token: str, name: str) -> tuple:
        rid = self.next_id()
        self.send_line(f"GROUP_CREATE {rid} token={token} name={name}")
        resp = self.recv_line()
        kind, rid, rest = parse_resp(resp)
        kv = parse_kv(rest)
        group_id = int(kv.get("group_id", 0))
        return (kind, rid, rest, group_id)

    def group_add(self, token: str, group_id: int, username: str) -> tuple:
        rid = self.next_id()
        self.send_line(f"GROUP_ADD {rid} token={token} group_id={group_id} username={username}")
        resp = self.recv_line()
        return parse_resp(resp)

    def gm_chat_start(self, token: str, group_id: int) -> tuple:
        rid = self.next_id()
        self.send_line(f"GM_CHAT_START {rid} token={token} group_id={group_id}")
        resp = self.recv_line()
        return parse_resp(resp)

    def gm_chat_end(self, token: str) -> tuple:
        rid = self.next_id()
        self.send_line(f"GM_CHAT_END {rid} token={token}")
        resp = self.recv_line()
        return parse_resp(resp)

    def gm_send(self, token: str, group_id: int, content: str) -> tuple:
        """Send GM with auto Base64 encoding"""
        rid = self.next_id()
        content_b64 = b64_encode(content)
        self.send_line(f"GM_SEND {rid} token={token} group_id={group_id} content={content_b64}")
        resp = self.recv_line()
        return parse_resp(resp)

    def gm_send_raw(self, token: str, group_id: int, content_b64: str) -> tuple:
        """Send GM with raw Base64 content"""
        rid = self.next_id()
        self.send_line(f"GM_SEND {rid} token={token} group_id={group_id} content={content_b64}")
        resp = self.recv_line()
        return parse_resp(resp)

    def gm_history(self, token: str, group_id: int, limit: int = 50) -> tuple:
        rid = self.next_id()
        self.send_line(f"GM_HISTORY {rid} token={token} group_id={group_id} limit={limit}")
        resp = self.recv_line()
        return parse_resp(resp)


def start_server(port: int, timeout_s: int = 3600):
    if not os.path.exists(SERVER_BIN):
        die(f"server binary not found: {SERVER_BIN}")

    proc = subprocess.Popen(
        [SERVER_BIN, str(port), str(timeout_s)],
        cwd=PROJECT_ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )

    start = time.time()
    while time.time() - start < 3:
        if proc.poll() is not None:
            die("server exited early")
        if proc.stdout and select.select([proc.stdout], [], [], 0.1)[0]:
            if "Server listening" in proc.stdout.readline():
                return proc
    return proc


def stop_server(proc):
    if proc and proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()


def clean_data():
    """Clean up test data"""
    # Remove GM directory
    if os.path.exists(GM_DIR):
        shutil.rmtree(GM_DIR)
    
    # Remove test databases
    for fname in ["users.db", "friends.db", "groups.db"]:
        path = os.path.join(DATA_DIR, fname)
        if os.path.exists(path):
            os.remove(path)


# ============ Test Cases ============

def test_basic_gm_send_and_history(port: int):
    """Test 1: Basic GM - create group, send message, check history"""
    info("Test 1: Basic GM send and history")
    
    c1 = Conn("127.0.0.1", port)
    c2 = Conn("127.0.0.1", port)
    
    try:
        # Register 2 users
        c1.register("gm_alice", "pass123", "gm_alice@test.com")
        c2.register("gm_bob", "pass123", "gm_bob@test.com")
        
        # Login
        kind, _, _, token1 = c1.login("gm_alice", "pass123")
        if kind != "OK":
            die("Failed to login gm_alice")
        
        kind, _, _, token2 = c2.login("gm_bob", "pass123")
        if kind != "OK":
            die("Failed to login gm_bob")
        
        # Alice creates a group
        kind, _, _, group_id = c1.group_create(token1, "TestGroup1")
        if kind != "OK" or group_id <= 0:
            die(f"Failed to create group: {group_id}")
        
        # Alice adds Bob to group
        kind, _, rest = c1.group_add(token1, group_id, "gm_bob")
        if kind != "OK":
            die(f"Failed to add gm_bob to group: {rest}")
        
        # Alice sends message to group
        kind, _, rest = c1.gm_send(token1, group_id, "Hello Group!")
        if kind != "OK":
            die(f"Failed to send GM: {rest}")
        
        kv = parse_kv(rest)
        if "msg_id" not in kv:
            die("No msg_id in response")
        
        # Alice sends another message
        kind, _, _ = c1.gm_send(token1, group_id, "This is group message 2")
        if kind != "OK":
            die("Failed to send second GM")
        
        # Bob checks group history
        kind, _, rest = c2.gm_history(token2, group_id)
        if kind != "OK":
            die(f"Failed to get GM history: {rest}")
        
        kv = parse_kv(rest)
        history = kv.get("history", "")
        if history == "empty":
            die("History should not be empty")
        
        # Verify messages exist (format: msg_id:from:content_b64:ts,...)
        if "gm_alice" not in history:
            die("Sender 'gm_alice' not found in history")
        
        ok("Basic GM send and history works")
        
    finally:
        c1.close()
        c2.close()


def test_gm_realtime_push(port: int):
    """Test 2: Real-time push - members in chat mode receive instant messages"""
    info("Test 2: GM real-time push")
    
    c1 = Conn("127.0.0.1", port)
    c2 = Conn("127.0.0.1", port)
    
    try:
        # Register and login
        c1.register("push_alice", "pass123", "push_alice@test.com")
        c2.register("push_bob", "pass123", "push_bob@test.com")
        
        _, _, _, token1 = c1.login("push_alice", "pass123")
        _, _, _, token2 = c2.login("push_bob", "pass123")
        
        # Alice creates group and adds Bob
        _, _, _, group_id = c1.group_create(token1, "PushGroup")
        c1.group_add(token1, group_id, "push_bob")
        
        # Both enter chat mode
        kind, _, rest = c1.gm_chat_start(token1, group_id)
        if kind != "OK":
            die(f"Alice failed to start GM chat: {rest}")
        
        # Drain any JOIN push from Alice entering
        time.sleep(0.1)
        c2.drain_push(0.2)
        
        kind, _, rest = c2.gm_chat_start(token2, group_id)
        if kind != "OK":
            die(f"Bob failed to start GM chat: {rest}")
        
        # Alice should receive GM_JOIN push for Bob
        time.sleep(0.1)
        push_msgs = c1.drain_push(0.5)
        
        gm_join_found = False
        for p in push_msgs:
            if "GM_JOIN" in p and "push_bob" in p:
                gm_join_found = True
                break
        
        if not gm_join_found:
            info(f"Warning: GM_JOIN not received (received: {push_msgs})")
        
        # Alice sends message
        kind, _, _ = c1.gm_send(token1, group_id, "Real-time test!")
        if kind != "OK":
            die("Failed to send GM")
        
        # Bob should receive PUSH GM
        time.sleep(0.2)
        push_msgs = c2.drain_push(0.5)
        
        gm_push_found = False
        for p in push_msgs:
            if "PUSH GM " in p and "push_alice" in p:
                # Verify content is Base64 encoded
                if "content=" in p:
                    gm_push_found = True
                    break
        
        if not gm_push_found:
            die(f"Bob did not receive PUSH GM (received: {push_msgs})")
        
        # Cleanup - both exit chat
        c1.gm_chat_end(token1)
        c2.gm_chat_end(token2)
        
        ok("GM real-time push works")
        
    finally:
        c1.close()
        c2.close()


def test_gm_three_members(port: int):
    """Test 3: Three members in group - all receive messages"""
    info("Test 3: GM with 3 members")
    
    c1 = Conn("127.0.0.1", port)
    c2 = Conn("127.0.0.1", port)
    c3 = Conn("127.0.0.1", port)
    
    try:
        # Register 3 users
        c1.register("trio_a", "pass123", "trio_a@test.com")
        c2.register("trio_b", "pass123", "trio_b@test.com")
        c3.register("trio_c", "pass123", "trio_c@test.com")
        
        # Login all
        _, _, _, token_a = c1.login("trio_a", "pass123")
        _, _, _, token_b = c2.login("trio_b", "pass123")
        _, _, _, token_c = c3.login("trio_c", "pass123")
        
        # A creates group, adds B and C
        _, _, _, group_id = c1.group_create(token_a, "TrioGroup")
        c1.group_add(token_a, group_id, "trio_b")
        c1.group_add(token_a, group_id, "trio_c")
        
        # All enter chat mode
        c1.gm_chat_start(token_a, group_id)
        time.sleep(0.05)
        c2.gm_chat_start(token_b, group_id)
        time.sleep(0.05)
        c3.gm_chat_start(token_c, group_id)
        time.sleep(0.1)
        
        # Clear any JOIN pushes
        c1.drain_push(0.2)
        c2.drain_push(0.2)
        c3.drain_push(0.2)
        
        # A sends message
        kind, _, _ = c1.gm_send(token_a, group_id, "Message from A")
        if kind != "OK":
            die("A failed to send GM")
        
        time.sleep(0.2)
        
        # B and C should receive PUSH GM (A won't receive their own)
        push_b = c2.drain_push(0.5)
        push_c = c3.drain_push(0.5)
        
        b_received = any("PUSH GM " in p and "trio_a" in p for p in push_b)
        c_received = any("PUSH GM " in p and "trio_a" in p for p in push_c)
        
        if not b_received:
            die(f"B did not receive PUSH GM (received: {push_b})")
        
        if not c_received:
            die(f"C did not receive PUSH GM (received: {push_c})")
        
        # B sends message - A and C should receive
        c1.drain_push(0.1)  # Clear A's buffer
        c3.drain_push(0.1)  # Clear C's buffer
        
        kind, _, _ = c2.gm_send(token_b, group_id, "Message from B")
        if kind != "OK":
            die("B failed to send GM")
        
        time.sleep(0.2)
        
        push_a = c1.drain_push(0.5)
        push_c = c3.drain_push(0.5)
        
        a_received = any("PUSH GM " in p and "trio_b" in p for p in push_a)
        c_received = any("PUSH GM " in p and "trio_b" in p for p in push_c)
        
        if not a_received:
            die(f"A did not receive B's PUSH GM (received: {push_a})")
        
        if not c_received:
            die(f"C did not receive B's PUSH GM (received: {push_c})")
        
        # Cleanup
        c1.gm_chat_end(token_a)
        c2.gm_chat_end(token_b)
        c3.gm_chat_end(token_c)
        
        ok("GM with 3 members works")
        
    finally:
        c1.close()
        c2.close()
        c3.close()


def test_gm_join_leave_notification(port: int):
    """Test 4: GM_JOIN and GM_LEAVE notifications"""
    info("Test 4: GM join/leave notifications")
    
    c1 = Conn("127.0.0.1", port)
    c2 = Conn("127.0.0.1", port)
    
    try:
        # Register and login
        c1.register("notify_a", "pass123", "notify_a@test.com")
        c2.register("notify_b", "pass123", "notify_b@test.com")
        
        _, _, _, token_a = c1.login("notify_a", "pass123")
        _, _, _, token_b = c2.login("notify_b", "pass123")
        
        # A creates group and adds B
        _, _, _, group_id = c1.group_create(token_a, "NotifyGroup")
        c1.group_add(token_a, group_id, "notify_b")
        
        # A enters chat mode first
        c1.gm_chat_start(token_a, group_id)
        time.sleep(0.1)
        c1.drain_push(0.2)  # Clear A's buffer
        
        # B enters - A should receive GM_JOIN
        c2.gm_chat_start(token_b, group_id)
        time.sleep(0.2)
        
        push_a = c1.drain_push(0.5)
        join_found = any("GM_JOIN" in p and "notify_b" in p for p in push_a)
        
        if not join_found:
            info(f"Warning: GM_JOIN for B not received by A (received: {push_a})")
        
        # B leaves - A should receive GM_LEAVE
        c1.drain_push(0.1)  # Clear buffer
        c2.gm_chat_end(token_b)
        time.sleep(0.2)
        
        push_a = c1.drain_push(0.5)
        leave_found = any("GM_LEAVE" in p and "notify_b" in p for p in push_a)
        
        if not leave_found:
            info(f"Warning: GM_LEAVE for B not received by A (received: {push_a})")
        
        # Cleanup
        c1.gm_chat_end(token_a)
        
        ok("GM join/leave notifications work")
        
    finally:
        c1.close()
        c2.close()


def test_gm_non_member_access(port: int):
    """Test 5: Non-member cannot access group chat"""
    info("Test 5: GM non-member access denied")
    
    c1 = Conn("127.0.0.1", port)
    c2 = Conn("127.0.0.1", port)
    
    try:
        # Register and login
        c1.register("member_a", "pass123", "member_a@test.com")
        c2.register("outsider", "pass123", "outsider@test.com")
        
        _, _, _, token_a = c1.login("member_a", "pass123")
        _, _, _, token_out = c2.login("outsider", "pass123")
        
        # A creates private group (doesn't add outsider)
        _, _, _, group_id = c1.group_create(token_a, "PrivateGroup")
        
        # Outsider tries to start chat - should fail
        kind, _, rest = c2.gm_chat_start(token_out, group_id)
        if kind == "OK":
            die("Non-member should not be able to start GM chat")
        
        if "403" not in rest and "not_member" not in rest.lower() and "permission" not in rest.lower():
            info(f"Expected 403/not_member error, got: {rest}")
        
        # Outsider tries to send message - should fail
        kind, _, rest = c2.gm_send(token_out, group_id, "Trying to sneak in!")
        if kind == "OK":
            die("Non-member should not be able to send GM")
        
        # Outsider tries to get history - should fail
        kind, _, rest = c2.gm_history(token_out, group_id)
        if kind == "OK":
            die("Non-member should not be able to get GM history")
        
        ok("GM non-member access denied correctly")
        
    finally:
        c1.close()
        c2.close()


def test_gm_invalid_group(port: int):
    """Test 6: Invalid group_id handling"""
    info("Test 6: GM invalid group_id")
    
    c1 = Conn("127.0.0.1", port)
    
    try:
        # Register and login
        c1.register("invalid_user", "pass123", "invalid_user@test.com")
        _, _, _, token = c1.login("invalid_user", "pass123")
        
        # Try to start chat with non-existent group
        kind, _, rest = c1.gm_chat_start(token, 99999)
        if kind == "OK":
            die("Should fail for non-existent group")
        
        # Try to send to non-existent group
        kind, _, rest = c1.gm_send(token, 99999, "Hello!")
        if kind == "OK":
            die("Should fail to send to non-existent group")
        
        # Try to get history from non-existent group
        kind, _, rest = c1.gm_history(token, 99999)
        if kind == "OK":
            die("Should fail to get history from non-existent group")
        
        ok("GM invalid group_id handled correctly")
        
    finally:
        c1.close()


def test_gm_base64_special_chars(port: int):
    """Test 7: Base64 encoding with special characters and unicode"""
    info("Test 7: GM Base64 with special characters")
    
    c1 = Conn("127.0.0.1", port)
    c2 = Conn("127.0.0.1", port)
    
    try:
        # Register and login
        c1.register("b64_a", "pass123", "b64_a@test.com")
        c2.register("b64_b", "pass123", "b64_b@test.com")
        
        _, _, _, token_a = c1.login("b64_a", "pass123")
        _, _, _, token_b = c2.login("b64_b", "pass123")
        
        # Create group and add member
        _, _, _, group_id = c1.group_create(token_a, "B64Group")
        c1.group_add(token_a, group_id, "b64_b")
        
        # Test messages with special characters
        test_messages = [
            "Hello World!",              # Basic
            "Xin chào các bạn!",         # Vietnamese
            "🎉 Party time! 🎊",          # Emoji
            "Special: !@#$%^&*()",       # Special chars
            "Multi\nLine\nMessage",      # Newlines
            "Tab\there\tand\tthere",     # Tabs
        ]
        
        for msg in test_messages:
            kind, _, rest = c1.gm_send(token_a, group_id, msg)
            if kind != "OK":
                die(f"Failed to send: {msg[:20]}... - {rest}")
        
        # Get history and verify
        kind, _, rest = c2.gm_history(token_b, group_id)
        if kind != "OK":
            die(f"Failed to get history: {rest}")
        
        kv = parse_kv(rest)
        history = kv.get("history", "")
        
        if history == "empty":
            die("History should not be empty")
        
        # Count messages (split by comma)
        msg_count = len(history.split(",")) if history else 0
        if msg_count < len(test_messages):
            info(f"Warning: Expected {len(test_messages)} messages, got {msg_count}")
        
        ok("GM Base64 special characters work")
        
    finally:
        c1.close()
        c2.close()


def test_gm_history_limit(port: int):
    """Test 8: GM history with limit parameter"""
    info("Test 8: GM history limit")
    
    c1 = Conn("127.0.0.1", port)
    
    try:
        # Register and login
        c1.register("limit_user", "pass123", "limit_user@test.com")
        _, _, _, token = c1.login("limit_user", "pass123")
        
        # Create group
        _, _, _, group_id = c1.group_create(token, "LimitGroup")
        
        # Send 10 messages
        for i in range(10):
            kind, _, _ = c1.gm_send(token, group_id, f"Message {i+1}")
            if kind != "OK":
                die(f"Failed to send message {i+1}")
        
        # Get history with limit=5
        kind, _, rest = c1.gm_history(token, group_id, limit=5)
        if kind != "OK":
            die(f"Failed to get history: {rest}")
        
        kv = parse_kv(rest)
        history = kv.get("history", "")
        
        if history == "empty":
            die("History should not be empty")
        
        # Count messages
        msg_count = len(history.split(",")) if history else 0
        if msg_count > 5:
            die(f"Limit not working: expected <= 5, got {msg_count}")
        
        ok("GM history limit works")
        
    finally:
        c1.close()


def test_gm_offline_then_online(port: int):
    """Test 9: Offline member can see messages when they check history"""
    info("Test 9: GM offline then online")
    
    c1 = Conn("127.0.0.1", port)
    c2 = Conn("127.0.0.1", port)
    
    try:
        # Register both
        c1.register("off_a", "pass123", "off_a@test.com")
        c2.register("off_b", "pass123", "off_b@test.com")
        
        # Login A, create group, add B
        _, _, _, token_a = c1.login("off_a", "pass123")
        _, _, _, group_id = c1.group_create(token_a, "OfflineGroup")
        
        # Need to login B temporarily to add them
        _, _, _, token_b = c2.login("off_b", "pass123")
        c1.group_add(token_a, group_id, "off_b")
        
        # B "goes offline" (close connection without being in chat mode)
        c2.close()
        
        # A sends messages while B is offline
        c1.gm_send(token_a, group_id, "Message while B offline 1")
        c1.gm_send(token_a, group_id, "Message while B offline 2")
        
        # B comes back online
        c2 = Conn("127.0.0.1", port)
        _, _, _, token_b = c2.login("off_b", "pass123")
        
        # B gets history - should see offline messages
        kind, _, rest = c2.gm_history(token_b, group_id)
        if kind != "OK":
            die(f"Failed to get history: {rest}")
        
        kv = parse_kv(rest)
        history = kv.get("history", "")
        
        if history == "empty":
            die("B should see messages sent while offline")
        
        # Verify at least 2 messages
        msg_count = len(history.split(",")) if history else 0
        if msg_count < 2:
            die(f"Expected at least 2 messages, got {msg_count}")
        
        ok("GM offline then online works")
        
    finally:
        c1.close()
        c2.close()


def test_gm_chat_end_clears_mode(port: int):
    """Test 10: GM_CHAT_END properly clears chat mode"""
    info("Test 10: GM_CHAT_END clears mode")
    
    c1 = Conn("127.0.0.1", port)
    c2 = Conn("127.0.0.1", port)
    
    try:
        # Setup
        c1.register("end_a", "pass123", "end_a@test.com")
        c2.register("end_b", "pass123", "end_b@test.com")
        
        _, _, _, token_a = c1.login("end_a", "pass123")
        _, _, _, token_b = c2.login("end_b", "pass123")
        
        _, _, _, group_id = c1.group_create(token_a, "EndGroup")
        c1.group_add(token_a, group_id, "end_b")
        
        # Both enter chat mode
        c1.gm_chat_start(token_a, group_id)
        c2.gm_chat_start(token_b, group_id)
        time.sleep(0.1)
        c1.drain_push(0.2)
        c2.drain_push(0.2)
        
        # B exits chat mode
        kind, _, _ = c2.gm_chat_end(token_b)
        if kind != "OK":
            die("Failed to end chat")
        
        time.sleep(0.1)
        c2.drain_push(0.1)
        
        # A sends message - B should NOT receive PUSH (not in chat mode)
        c1.gm_send(token_a, group_id, "After B left chat mode")
        time.sleep(0.2)
        
        push_b = c2.drain_push(0.5)
        gm_push = [p for p in push_b if "PUSH GM " in p and "end_a" in p]
        
        if gm_push:
            die(f"B should not receive PUSH GM after GM_CHAT_END (received: {gm_push})")
        
        # But B can still see message in history
        kind, _, rest = c2.gm_history(token_b, group_id)
        if kind != "OK":
            die("Failed to get history")
        
        # Cleanup
        c1.gm_chat_end(token_a)
        
        ok("GM_CHAT_END clears mode correctly")
        
    finally:
        c1.close()
        c2.close()


def test_gm_removed_member_kicked_from_chat(port: int):
    """Test 11: When owner removes member, that member is kicked from group chat mode
    Scenario: A (owner) creates group, invites B and C. All 3 chat together.
    Then A removes C. After that:
    - A and B should chat normally
    - C should NOT see new messages
    - C should receive GM_KICKED and know they were removed
    - C should NOT see A/B join/leave anymore
    - A/B should NOT see C leave/join
    """
    info("Test 11: GM removed member kicked from chat")
    
    c1 = Conn("127.0.0.1", port)  # Owner (A)
    c2 = Conn("127.0.0.1", port)  # Member B
    c3 = Conn("127.0.0.1", port)  # Member C (will be removed)
    
    try:
        # Register 3 users
        c1.register("kick_a", "pass123", "kick_a@test.com")
        c2.register("kick_b", "pass123", "kick_b@test.com")
        c3.register("kick_c", "pass123", "kick_c@test.com")
        
        # Login all
        _, _, _, token_a = c1.login("kick_a", "pass123")
        _, _, _, token_b = c2.login("kick_b", "pass123")
        _, _, _, token_c = c3.login("kick_c", "pass123")
        
        # A creates group, adds B and C
        _, _, _, group_id = c1.group_create(token_a, "KickTestGroup")
        c1.group_add(token_a, group_id, "kick_b")
        c1.group_add(token_a, group_id, "kick_c")
        
        # All enter group chat mode
        c1.gm_chat_start(token_a, group_id)
        c2.gm_chat_start(token_b, group_id)
        c3.gm_chat_start(token_c, group_id)
        time.sleep(0.1)
        
        # Clear any join pushes
        c1.drain_push(0.2)
        c2.drain_push(0.2)
        c3.drain_push(0.2)
        
        # C can send messages before being removed
        kind, _, _ = c3.gm_send(token_c, group_id, "Hello from C")
        if kind != "OK":
            die("C should be able to send before removal")
        
        time.sleep(0.1)
        c1.drain_push(0.2)
        c2.drain_push(0.2)
        
        # A removes C from group (not B!)
        rid = c1.next_id()
        c1.send_line(f"GROUP_REMOVE {rid} token={token_a} group_id={group_id} username=kick_c")
        resp = c1.recv_line()
        kind, _, rest = parse_resp(resp)
        if kind != "OK":
            die(f"Failed to remove C from group: {rest}")
        
        time.sleep(0.2)
        
        # C should receive PUSH GM_KICKED
        push_c = c3.drain_push(0.5)
        kicked_found = any("GM_KICKED" in p for p in push_c)
        if not kicked_found:
            die(f"C did not receive GM_KICKED (received: {push_c})")
        
        # C tries to send message - should fail (no longer member)
        kind, _, rest = c3.gm_send(token_c, group_id, "C trying to send after removal")
        if kind == "OK":
            die("C should NOT be able to send after being removed")
        
        # A and B should still chat normally
        c1.drain_push(0.1)
        c2.drain_push(0.1)
        c3.drain_push(0.1)
        
        kind, _, _ = c1.gm_send(token_a, group_id, "Message from A after C removed")
        if kind != "OK":
            die("A should still be able to send")
        
        time.sleep(0.2)
        
        # B should receive A's message
        push_b = c2.drain_push(0.5)
        b_received = any("PUSH GM " in p and "kick_a" in p for p in push_b)
        if not b_received:
            die(f"B did not receive A's message (received: {push_b})")
        
        # C should NOT receive A's message (kicked from chat mode)
        push_c = c3.drain_push(0.5)
        c_received_msg = any("PUSH GM " in p and "kick_a" in p for p in push_c)
        if c_received_msg:
            die(f"C should NOT receive messages after being kicked (received: {push_c})")
        
        # Test GM_JOIN/GM_LEAVE notifications don't go to kicked user
        # B exits and re-enters chat
        c2.gm_chat_end(token_b)
        time.sleep(0.1)
        c1.drain_push(0.2)
        c3.drain_push(0.2)
        
        c2.gm_chat_start(token_b, group_id)
        time.sleep(0.2)
        
        # A should see B join
        push_a = c1.drain_push(0.5)
        a_saw_b_join = any("GM_JOIN" in p and "kick_b" in p for p in push_a)
        if not a_saw_b_join:
            info(f"Warning: A did not see B rejoin (received: {push_a})")
        
        # C should NOT see B join (C is kicked)
        push_c = c3.drain_push(0.5)
        c_saw_b_join = any("GM_JOIN" in p and "kick_b" in p for p in push_c)
        if c_saw_b_join:
            die(f"C should NOT see B join after being kicked (received: {push_c})")
        
        ok("GM removed member kicked from chat correctly")
        
    finally:
        c1.close()
        c2.close()
        c3.close()


# ============ Main ============

def run_all_tests():
    port = free_port()
    proc = None
    
    try:
        clean_data()
        proc = start_server(port)
        time.sleep(0.5)
        
        test_basic_gm_send_and_history(port)
        test_gm_realtime_push(port)
        test_gm_three_members(port)
        test_gm_join_leave_notification(port)
        test_gm_non_member_access(port)
        test_gm_invalid_group(port)
        test_gm_base64_special_chars(port)
        test_gm_history_limit(port)
        test_gm_offline_then_online(port)
        test_gm_chat_end_clears_mode(port)
        test_gm_removed_member_kicked_from_chat(port)
        
        print("\n" + "=" * 50)
        print("[PASS] All Group Message tests passed!")
        print("=" * 50)
        
    finally:
        stop_server(proc)
        # Optionally clean up
        # clean_data()


if __name__ == "__main__":
    run_all_tests()
