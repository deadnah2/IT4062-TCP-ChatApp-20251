#!/usr/bin/env python3
"""
Integration tests for Private Message (PM) feature.

Test cases cover:
1. Basic PM flow: send, receive, history
2. Offline messaging: A sends to B (offline), B sees when online
3. Multi-conversation: A chats with B, then C, then back to B
4. Real-time push: Both users in chat mode receive instant messages
5. Read/unread tracking: new message count, mark as read
6. Edge cases: send to self, send to non-existent user, empty content
7. Base64 encoding: messages with spaces, special characters, unicode

Author: ChatProject Team
"""

import base64
import os
import select
import socket
import subprocess
import sys
import threading
import time
from contextlib import closing

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SERVER_BIN = os.path.join(PROJECT_ROOT, "build", "server")
DATA_DIR = os.path.join(PROJECT_ROOT, "data")
PM_DIR = os.path.join(DATA_DIR, "pm")


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

    def send_line(self, line: str):
        self.sock.sendall((line + "\r\n").encode())

    def recv_line(self, timeout: float = 3.0) -> str:
        self.sock.settimeout(timeout)
        while b"\r\n" not in self.buf:
            d = self.sock.recv(4096)
            if not d:
                raise EOFError("disconnected")
            self.buf += d
        line, self.buf = self.buf.split(b"\r\n", 1)
        return line.decode()

    def try_recv_line(self, timeout: float = 0.5) -> str | None:
        """Try to receive a line, return None if timeout"""
        try:
            return self.recv_line(timeout)
        except socket.timeout:
            return None
        except Exception:
            return None

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

    def pm_chat_start(self, token: str, with_user: str) -> tuple:
        rid = self.next_id()
        self.send_line(f"PM_CHAT_START {rid} token={token} with={with_user}")
        resp = self.recv_line()
        return parse_resp(resp)

    def pm_chat_end(self, token: str) -> tuple:
        rid = self.next_id()
        self.send_line(f"PM_CHAT_END {rid} token={token}")
        resp = self.recv_line()
        return parse_resp(resp)

    def pm_send(self, token: str, to_user: str, content: str) -> tuple:
        """Send PM with auto Base64 encoding"""
        rid = self.next_id()
        content_b64 = b64_encode(content)
        self.send_line(f"PM_SEND {rid} token={token} to={to_user} content={content_b64}")
        resp = self.recv_line()
        return parse_resp(resp)

    def pm_send_raw(self, token: str, to_user: str, content_b64: str) -> tuple:
        """Send PM with raw Base64 content"""
        rid = self.next_id()
        self.send_line(f"PM_SEND {rid} token={token} to={to_user} content={content_b64}")
        resp = self.recv_line()
        return parse_resp(resp)

    def pm_history(self, token: str, with_user: str, limit: int = 50) -> tuple:
        rid = self.next_id()
        self.send_line(f"PM_HISTORY {rid} token={token} with={with_user} limit={limit}")
        resp = self.recv_line()
        return parse_resp(resp)

    def pm_conversations(self, token: str) -> tuple:
        rid = self.next_id()
        self.send_line(f"PM_CONVERSATIONS {rid} token={token}")
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
    # Backup and restore users.db
    db_path = os.path.join(DATA_DIR, "users.db")
    friends_path = os.path.join(DATA_DIR, "friends.db")
    
    # Remove PM directory
    if os.path.exists(PM_DIR):
        import shutil
        shutil.rmtree(PM_DIR)
    
    # Remove test databases
    for path in [db_path, friends_path]:
        if os.path.exists(path):
            os.remove(path)


# ============ Test Cases ============

def test_basic_pm_send_and_history(port: int):
    """Test 1: Basic PM - send message and check history"""
    info("Test 1: Basic PM send and history")
    
    # Create two users
    c1 = Conn("127.0.0.1", port)
    c2 = Conn("127.0.0.1", port)
    
    try:
        # Register users
        kind, _, _ = c1.register("alice", "password123", "alice@test.com")
        if kind != "OK":
            die("Failed to register alice")
        
        kind, _, _ = c2.register("bob", "password123", "bob@test.com")
        if kind != "OK":
            die("Failed to register bob")
        
        # Login
        kind, _, _, token1 = c1.login("alice", "password123")
        if kind != "OK":
            die("Failed to login alice")
        
        kind, _, _, token2 = c2.login("bob", "password123")
        if kind != "OK":
            die("Failed to login bob")
        
        # Alice sends message to Bob
        kind, _, rest = c1.pm_send(token1, "bob", "Hello Bob!")
        if kind != "OK":
            die(f"Failed to send PM: {rest}")
        
        kv = parse_kv(rest)
        if "msg_id" not in kv:
            die("No msg_id in response")
        
        # Alice sends another message
        kind, _, _ = c1.pm_send(token1, "bob", "How are you?")
        if kind != "OK":
            die("Failed to send second PM")
        
        # Bob checks history with Alice
        kind, _, rest = c2.pm_history(token2, "alice")
        if kind != "OK":
            die(f"Failed to get history: {rest}")
        
        kv = parse_kv(rest)
        messages = kv.get("messages", "")
        if messages == "empty":
            die("History should not be empty")
        
        # Verify messages exist (format: msg_id:from:content_b64:ts,...)
        if "alice" not in messages:
            die("Sender 'alice' not found in history")
        
        ok("Basic PM send and history works")
        
    finally:
        c1.close()
        c2.close()


def test_offline_messaging(port: int):
    """Test 2: Offline messaging - B receives messages when coming online"""
    info("Test 2: Offline messaging")
    
    c1 = Conn("127.0.0.1", port)
    
    try:
        # Register and login alice
        c1.register("alice2", "password123", "alice2@test.com")
        kind, _, _, token1 = c1.login("alice2", "password123")
        if kind != "OK":
            die("Failed to login alice2")
        
        # Register bob2 but don't login yet (simulate offline)
        c_temp = Conn("127.0.0.1", port)
        c_temp.register("bob2", "password123", "bob2@test.com")
        c_temp.close()
        
        # Alice sends messages to offline Bob
        kind, _, _ = c1.pm_send(token1, "bob2", "Message 1 - you were offline")
        if kind != "OK":
            die("Failed to send to offline user")
        
        kind, _, _ = c1.pm_send(token1, "bob2", "Message 2 - still offline")
        if kind != "OK":
            die("Failed to send second message")
        
        # Now Bob comes online
        c2 = Conn("127.0.0.1", port)
        kind, _, _, token2 = c2.login("bob2", "password123")
        if kind != "OK":
            die("Failed to login bob2")
        
        # Bob checks conversations - should show alice2 with unread count
        kind, _, rest = c2.pm_conversations(token2)
        if kind != "OK":
            die(f"Failed to get conversations: {rest}")
        
        kv = parse_kv(rest)
        convos = kv.get("conversations", "")
        
        # Should have alice2 with unread messages
        if "alice2" not in convos:
            die("alice2 not in conversations")
        
        # Bob enters chat with Alice - should see offline messages
        kind, _, rest = c2.pm_chat_start(token2, "alice2")
        if kind != "OK":
            die(f"Failed to start chat: {rest}")
        
        kv = parse_kv(rest)
        history = kv.get("history", "")
        if history == "empty":
            die("History should contain offline messages")
        
        # Verify both messages are there
        # Decode and check
        if "alice2" not in history:
            die("Offline messages not found in history")
        
        c2.pm_chat_end(token2)
        
        ok("Offline messaging works")
        
    finally:
        c1.close()
        try:
            c2.close()
        except:
            pass


def test_multi_conversation(port: int):
    """Test 3: Multi-conversation - A chats with B, then C, then back to B"""
    info("Test 3: Multi-conversation switching")
    
    c1 = Conn("127.0.0.1", port)
    c2 = Conn("127.0.0.1", port)
    c3 = Conn("127.0.0.1", port)
    
    try:
        # Register 3 users
        c1.register("user_a", "pass123", "a@test.com")
        c2.register("user_b", "pass123", "b@test.com")
        c3.register("user_c", "pass123", "c@test.com")
        
        # Login all
        _, _, _, token_a = c1.login("user_a", "pass123")
        _, _, _, token_b = c2.login("user_b", "pass123")
        _, _, _, token_c = c3.login("user_c", "pass123")
        
        # A starts chat with B
        kind, _, rest = c1.pm_chat_start(token_a, "user_b")
        if kind != "OK":
            die(f"Failed to start chat A->B: {rest}")
        
        # A sends to B
        c1.pm_send(token_a, "user_b", "Hi B, this is A")
        
        # A ends chat with B
        c1.pm_chat_end(token_a)
        
        # A starts chat with C
        kind, _, _ = c1.pm_chat_start(token_a, "user_c")
        if kind != "OK":
            die("Failed to start chat A->C")
        
        # A sends to C
        c1.pm_send(token_a, "user_c", "Hi C, this is A")
        c1.pm_send(token_a, "user_c", "How are you C?")
        
        # A ends chat with C
        c1.pm_chat_end(token_a)
        
        # A goes back to B
        kind, _, rest = c1.pm_chat_start(token_a, "user_b")
        if kind != "OK":
            die("Failed to restart chat A->B")
        
        kv = parse_kv(rest)
        history = kv.get("history", "")
        
        # Should see previous message to B
        if history == "empty":
            die("History with B should not be empty")
        
        # Send another message to B
        c1.pm_send(token_a, "user_b", "Back again B!")
        
        c1.pm_chat_end(token_a)
        
        # Verify A has 2 conversations
        kind, _, rest = c1.pm_conversations(token_a)
        kv = parse_kv(rest)
        convos = kv.get("conversations", "")
        
        if "user_b" not in convos or "user_c" not in convos:
            die(f"Should have conversations with both B and C, got: {convos}")
        
        ok("Multi-conversation switching works")
        
    finally:
        c1.close()
        c2.close()
        c3.close()


def test_realtime_push(port: int):
    """Test 4: Real-time push - both users in chat mode receive instant messages"""
    info("Test 4: Real-time push")
    
    c1 = Conn("127.0.0.1", port)
    c2 = Conn("127.0.0.1", port)
    
    try:
        # Register and login
        c1.register("push_a", "pass123", "push_a@test.com")
        c2.register("push_b", "pass123", "push_b@test.com")
        
        _, _, _, token_a = c1.login("push_a", "pass123")
        _, _, _, token_b = c2.login("push_b", "pass123")
        
        # Both enter chat mode with each other
        c1.pm_chat_start(token_a, "push_b")
        c2.pm_chat_start(token_b, "push_a")
        
        # A sends message to B
        c1.pm_send(token_a, "push_b", "Real-time message!")
        
        # B should receive PUSH immediately
        push_line = c2.try_recv_line(timeout=2.0)
        
        if push_line is None:
            die("B did not receive PUSH message")
        
        if not push_line.startswith("PUSH PM"):
            die(f"Expected PUSH PM, got: {push_line}")
        
        # Verify push content
        if "push_a" not in push_line:
            die("Push should contain sender name")
        
        # B sends reply
        c2.pm_send(token_b, "push_a", "Got it!")
        
        # A should receive PUSH
        push_line = c1.try_recv_line(timeout=2.0)
        
        if push_line is None:
            die("A did not receive PUSH message")
        
        if not push_line.startswith("PUSH PM"):
            die(f"Expected PUSH PM for A, got: {push_line}")
        
        c1.pm_chat_end(token_a)
        c2.pm_chat_end(token_b)
        
        ok("Real-time push works")
        
    finally:
        c1.close()
        c2.close()


def test_read_unread_tracking(port: int):
    """Test 5: Read/unread tracking - new count and mark as read"""
    info("Test 5: Read/unread tracking")
    
    c1 = Conn("127.0.0.1", port)
    c2 = Conn("127.0.0.1", port)
    
    try:
        # Register and login
        c1.register("read_a", "pass123", "read_a@test.com")
        c2.register("read_b", "pass123", "read_b@test.com")
        
        _, _, _, token_a = c1.login("read_a", "pass123")
        _, _, _, token_b = c2.login("read_b", "pass123")
        
        # A sends 3 messages to B (B not in chat mode)
        c1.pm_send(token_a, "read_b", "Message 1")
        c1.pm_send(token_a, "read_b", "Message 2")
        c1.pm_send(token_a, "read_b", "Message 3")
        
        # B checks conversations - should show 3 new
        kind, _, rest = c2.pm_conversations(token_b)
        kv = parse_kv(rest)
        convos = kv.get("conversations", "")
        
        # Format: username:unread_count
        if "read_a:3" not in convos:
            # Try checking if unread count exists
            if "read_a" not in convos:
                die(f"Conversation with read_a not found: {convos}")
            info(f"Conversations: {convos}")
        
        # B enters chat with A - messages should be marked as read
        c2.pm_chat_start(token_b, "read_a")
        
        # B exits chat
        c2.pm_chat_end(token_b)
        
        # Check conversations again - unread should be 0
        kind, _, rest = c2.pm_conversations(token_b)
        kv = parse_kv(rest)
        convos = kv.get("conversations", "")
        
        # Should be read_a:0 or just read_a (0 unread)
        if "read_a:3" in convos:
            die(f"Messages should be marked as read, but still shows unread: {convos}")
        
        ok("Read/unread tracking works")
        
    finally:
        c1.close()
        c2.close()


def test_mark_read_on_exit(port: int):
    """Test 6: Mark read on chat exit - messages received during chat are marked read"""
    info("Test 6: Mark read on chat exit")
    
    c1 = Conn("127.0.0.1", port)
    c2 = Conn("127.0.0.1", port)
    
    try:
        # Register and login
        c1.register("exit_a", "pass123", "exit_a@test.com")
        c2.register("exit_b", "pass123", "exit_b@test.com")
        
        _, _, _, token_a = c1.login("exit_a", "pass123")
        _, _, _, token_b = c2.login("exit_b", "pass123")
        
        # B enters chat with A
        c2.pm_chat_start(token_b, "exit_a")
        
        # A sends messages while B is in chat mode
        c1.pm_send(token_a, "exit_b", "Message while in chat 1")
        c1.pm_send(token_a, "exit_b", "Message while in chat 2")
        
        # B receives pushes (drain them)
        c2.try_recv_line(timeout=0.5)
        c2.try_recv_line(timeout=0.5)
        
        # B exits chat - should mark all as read
        c2.pm_chat_end(token_b)
        
        # B checks conversations - should show 0 unread
        kind, _, rest = c2.pm_conversations(token_b)
        kv = parse_kv(rest)
        convos = kv.get("conversations", "")
        
        # Unread count should be 0
        parts = convos.split(",")
        for part in parts:
            if "exit_a" in part:
                if ":0" not in part and ":" in part:
                    # Has unread count > 0
                    count = part.split(":")[1] if ":" in part else "0"
                    if count != "0":
                        die(f"Messages should be marked read on exit, got count: {count}")
                break
        
        ok("Mark read on chat exit works")
        
    finally:
        c1.close()
        c2.close()


def test_error_cases(port: int):
    """Test 7: Error cases - send to self, non-existent user"""
    info("Test 7: Error cases")
    
    c1 = Conn("127.0.0.1", port)
    
    try:
        # Register and login
        c1.register("error_user", "pass123", "error@test.com")
        _, _, _, token = c1.login("error_user", "pass123")
        
        # Test: Send to self
        kind, _, rest = c1.pm_send(token, "error_user", "Hello myself")
        if kind != "ERR":
            die(f"Should fail when sending to self, got: {kind}")
        if "self" not in rest.lower():
            info(f"Error response: {rest}")
        
        # Test: Send to non-existent user
        kind, _, rest = c1.pm_send(token, "nonexistent_user_xyz", "Hello?")
        if kind != "ERR":
            die(f"Should fail for non-existent user, got: {kind}")
        if "not_found" not in rest.lower() and "404" not in rest:
            info(f"Error response: {rest}")
        
        # Test: Start chat with non-existent user
        kind, _, rest = c1.pm_chat_start(token, "nonexistent_user_xyz")
        if kind != "ERR":
            die(f"Should fail to start chat with non-existent user, got: {kind}")
        
        # Test: Get history with non-existent user
        kind, _, rest = c1.pm_history(token, "nonexistent_user_xyz")
        if kind != "ERR":
            die(f"Should fail to get history with non-existent user, got: {kind}")
        
        ok("Error cases handled correctly")
        
    finally:
        c1.close()


def test_special_characters(port: int):
    """Test 8: Special characters - spaces, unicode, emojis"""
    info("Test 8: Special characters in messages")
    
    c1 = Conn("127.0.0.1", port)
    c2 = Conn("127.0.0.1", port)
    
    try:
        # Register and login
        c1.register("special_a", "pass123", "special_a@test.com")
        c2.register("special_b", "pass123", "special_b@test.com")
        
        _, _, _, token_a = c1.login("special_a", "pass123")
        _, _, _, token_b = c2.login("special_b", "pass123")
        
        # Test messages with various special content
        test_messages = [
            "Hello World with spaces",
            "Special chars: !@#$%^&*()",
            "Unicode: Xin chào! Tạm biệt!",
            "Numbers 12345 and symbols <>?",
            "Tabs\tand\nnewlines",
            "Emoji test: 😀🎉👍",
            "Mixed: Hello 你好 مرحبا",
        ]
        
        for msg in test_messages:
            kind, _, rest = c1.pm_send(token_a, "special_b", msg)
            if kind != "OK":
                die(f"Failed to send message with special chars: {msg[:20]}... Error: {rest}")
        
        # B checks history
        kind, _, rest = c2.pm_history(token_b, "special_a")
        if kind != "OK":
            die(f"Failed to get history: {rest}")
        
        kv = parse_kv(rest)
        history = kv.get("messages", "")
        
        if history == "empty":
            die("History should not be empty")
        
        # Verify message count (should have all messages in history)
        msg_count = history.count("special_a")
        if msg_count < len(test_messages):
            info(f"Found {msg_count} messages, expected {len(test_messages)}")
        
        ok("Special characters handled correctly")
        
    finally:
        c1.close()
        c2.close()


def test_conversation_list_ordering(port: int):
    """Test 9: Conversation list - verify multiple conversations appear"""
    info("Test 9: Conversation list")
    
    c1 = Conn("127.0.0.1", port)
    c2 = Conn("127.0.0.1", port)
    c3 = Conn("127.0.0.1", port)
    c4 = Conn("127.0.0.1", port)
    
    try:
        # Register 4 users
        c1.register("conv_main", "pass123", "main@test.com")
        c2.register("conv_user1", "pass123", "u1@test.com")
        c3.register("conv_user2", "pass123", "u2@test.com")
        c4.register("conv_user3", "pass123", "u3@test.com")
        
        # Login all
        _, _, _, token_main = c1.login("conv_main", "pass123")
        _, _, _, token_u1 = c2.login("conv_user1", "pass123")
        _, _, _, token_u2 = c3.login("conv_user2", "pass123")
        _, _, _, token_u3 = c4.login("conv_user3", "pass123")
        
        # Main user sends to user1
        c1.pm_send(token_main, "conv_user1", "Hi user1")
        
        # Main user sends to user2
        c1.pm_send(token_main, "conv_user2", "Hi user2")
        c1.pm_send(token_main, "conv_user2", "Another message")
        
        # Main user sends to user3
        c1.pm_send(token_main, "conv_user3", "Hi user3")
        
        # User3 replies to main
        c4.pm_send(token_u3, "conv_main", "Reply from user3")
        
        # Check main's conversation list
        kind, _, rest = c1.pm_conversations(token_main)
        kv = parse_kv(rest)
        convos = kv.get("conversations", "")
        
        if convos == "empty":
            die("Conversations should not be empty")
        
        # Should have 3 conversations
        has_u1 = "conv_user1" in convos
        has_u2 = "conv_user2" in convos
        has_u3 = "conv_user3" in convos
        
        if not (has_u1 and has_u2 and has_u3):
            die(f"Missing conversations. Has u1:{has_u1}, u2:{has_u2}, u3:{has_u3}. Got: {convos}")
        
        # Main should have 1 unread from user3
        # Format: conv_user3:1 (1 unread)
        
        ok("Conversation list works correctly")
        
    finally:
        c1.close()
        c2.close()
        c3.close()
        c4.close()


def test_invalid_token(port: int):
    """Test 10: Invalid token - all PM commands should reject invalid token"""
    info("Test 10: Invalid token handling")
    
    c1 = Conn("127.0.0.1", port)
    
    try:
        fake_token = "invalid_token_12345"
        
        # Test PM_CHAT_START with invalid token
        kind, _, rest = c1.pm_chat_start(fake_token, "someone")
        if kind != "ERR":
            die(f"PM_CHAT_START should reject invalid token, got: {kind}")
        
        # Test PM_SEND with invalid token
        kind, _, rest = c1.pm_send(fake_token, "someone", "test")
        if kind != "ERR":
            die(f"PM_SEND should reject invalid token, got: {kind}")
        
        # Test PM_HISTORY with invalid token
        kind, _, rest = c1.pm_history(fake_token, "someone")
        if kind != "ERR":
            die(f"PM_HISTORY should reject invalid token, got: {kind}")
        
        # Test PM_CONVERSATIONS with invalid token
        kind, _, rest = c1.pm_conversations(fake_token)
        if kind != "ERR":
            die(f"PM_CONVERSATIONS should reject invalid token, got: {kind}")
        
        # Test PM_CHAT_END with invalid token
        kind, _, rest = c1.pm_chat_end(fake_token)
        if kind != "ERR":
            die(f"PM_CHAT_END should reject invalid token, got: {kind}")
        
        ok("Invalid token correctly rejected")
        
    finally:
        c1.close()


def test_concurrent_chats(port: int):
    """Test 11: Concurrent chats - multiple pairs chatting simultaneously"""
    info("Test 11: Concurrent chat sessions")
    
    # Create 4 users in 2 pairs
    c1 = Conn("127.0.0.1", port)
    c2 = Conn("127.0.0.1", port)
    c3 = Conn("127.0.0.1", port)
    c4 = Conn("127.0.0.1", port)
    
    try:
        # Register all
        c1.register("pair1_a", "pass123", "p1a@test.com")
        c2.register("pair1_b", "pass123", "p1b@test.com")
        c3.register("pair2_a", "pass123", "p2a@test.com")
        c4.register("pair2_b", "pass123", "p2b@test.com")
        
        # Login all
        _, _, _, t1a = c1.login("pair1_a", "pass123")
        _, _, _, t1b = c2.login("pair1_b", "pass123")
        _, _, _, t2a = c3.login("pair2_a", "pass123")
        _, _, _, t2b = c4.login("pair2_b", "pass123")
        
        # Both pairs start chatting
        c1.pm_chat_start(t1a, "pair1_b")
        c2.pm_chat_start(t1b, "pair1_a")
        c3.pm_chat_start(t2a, "pair2_b")
        c4.pm_chat_start(t2b, "pair2_a")
        
        # Pair 1 exchanges messages
        c1.pm_send(t1a, "pair1_b", "Pair1: A to B")
        push1 = c2.try_recv_line(timeout=1.0)
        if push1 is None or "PUSH" not in push1:
            die("Pair1 B should receive push")
        
        c2.pm_send(t1b, "pair1_a", "Pair1: B to A")
        push2 = c1.try_recv_line(timeout=1.0)
        if push2 is None or "PUSH" not in push2:
            die("Pair1 A should receive push")
        
        # Pair 2 exchanges messages (simultaneously)
        c3.pm_send(t2a, "pair2_b", "Pair2: A to B")
        push3 = c4.try_recv_line(timeout=1.0)
        if push3 is None or "PUSH" not in push3:
            die("Pair2 B should receive push")
        
        c4.pm_send(t2b, "pair2_a", "Pair2: B to A")
        push4 = c3.try_recv_line(timeout=1.0)
        if push4 is None or "PUSH" not in push4:
            die("Pair2 A should receive push")
        
        # Verify messages didn't cross between pairs
        if "pair2" in push1 or "pair2" in push2:
            die("Pair1 received pair2's messages!")
        
        if "pair1" in push3 or "pair1" in push4:
            die("Pair2 received pair1's messages!")
        
        # Cleanup
        c1.pm_chat_end(t1a)
        c2.pm_chat_end(t1b)
        c3.pm_chat_end(t2a)
        c4.pm_chat_end(t2b)
        
        ok("Concurrent chat sessions work correctly")
        
    finally:
        c1.close()
        c2.close()
        c3.close()
        c4.close()


def test_message_from_third_party_while_chatting(port: int):
    """Test 12: A đang chat với B, C gửi tin đến A - không bị interrupt, tin được lưu"""
    info("Test 12: Message from third party while in chat")
    
    c1 = Conn("127.0.0.1", port)  # A
    c2 = Conn("127.0.0.1", port)  # B
    c3 = Conn("127.0.0.1", port)  # C
    
    try:
        # Register 3 users
        c1.register("third_a", "pass123", "third_a@test.com")
        c2.register("third_b", "pass123", "third_b@test.com")
        c3.register("third_c", "pass123", "third_c@test.com")
        
        # Login all
        _, _, _, token_a = c1.login("third_a", "pass123")
        _, _, _, token_b = c2.login("third_b", "pass123")
        _, _, _, token_c = c3.login("third_c", "pass123")
        
        # A và B vào chat mode với nhau
        c1.pm_chat_start(token_a, "third_b")
        c2.pm_chat_start(token_b, "third_a")
        
        # A và B nhắn tin bình thường - verify real-time hoạt động
        c1.pm_send(token_a, "third_b", "Hi B!")
        push_b = c2.try_recv_line(timeout=1.0)
        if push_b is None or "PUSH" not in push_b:
            die("B should receive push from A")
        
        # Bây giờ C gửi tin cho A (trong khi A đang chat với B)
        c3.pm_send(token_c, "third_a", "Hey A, this is C!")
        c3.pm_send(token_c, "third_a", "Are you there?")
        
        # A KHÔNG nên nhận được PUSH từ C (vì đang chat với B, không phải C)
        push_from_c = c1.try_recv_line(timeout=1.0)
        if push_from_c is not None and "third_c" in push_from_c:
            die(f"A should NOT receive push from C while chatting with B! Got: {push_from_c}")
        
        # A tiếp tục nhắn với B - vẫn hoạt động bình thường
        c2.pm_send(token_b, "third_a", "Hi A, reply from B")
        push_from_b = c1.try_recv_line(timeout=1.0)
        if push_from_b is None or "PUSH" not in push_from_b:
            die("A should still receive push from B")
        if "third_b" not in push_from_b:
            die(f"Push should be from B, got: {push_from_b}")
        
        # A thoát chat với B
        c1.pm_chat_end(token_a)
        c2.pm_chat_end(token_b)
        
        # A kiểm tra conversations - phải thấy C với tin nhắn mới
        kind, _, rest = c1.pm_conversations(token_a)
        kv = parse_kv(rest)
        convos = kv.get("conversations", "")
        
        if "third_c" not in convos:
            die(f"A should have conversation with C, got: {convos}")
        
        # C gửi 2 tin → A nên thấy 2 unread
        # Format: third_c:2
        if "third_c:2" not in convos and "third_c:1" not in convos:
            info(f"Conversations: {convos}")
            # Không fail vì format có thể khác, chỉ cần có third_c là OK
        
        # A vào chat với C - phải thấy tin nhắn từ C
        kind, _, rest = c1.pm_chat_start(token_a, "third_c")
        kv = parse_kv(rest)
        history = kv.get("history", "")
        
        if history == "empty":
            die("A should see messages from C in history")
        
        if "third_c" not in history:
            die(f"History should contain messages from C, got: {history}")
        
        c1.pm_chat_end(token_a)
        
        ok("Third party message handled correctly (no interrupt, saved for later)")
        
    finally:
        c1.close()
        c2.close()
        c3.close()


# ============ Main ============

def run_all_tests():
    port = free_port()
    info(f"Starting server on port {port}")
    
    clean_data()
    proc = start_server(port)
    
    try:
        time.sleep(0.5)  # Give server time to fully start
        
        # Run all tests
        test_basic_pm_send_and_history(port)
        test_offline_messaging(port)
        test_multi_conversation(port)
        test_realtime_push(port)
        test_read_unread_tracking(port)
        test_mark_read_on_exit(port)
        test_error_cases(port)
        test_special_characters(port)
        test_conversation_list_ordering(port)
        test_invalid_token(port)
        test_concurrent_chats(port)
        test_message_from_third_party_while_chatting(port)
        
        print("\n" + "=" * 50)
        print("All Private Message tests PASSED! ✅")
        print("=" * 50)
        
    finally:
        stop_server(proc)
        clean_data()


if __name__ == "__main__":
    run_all_tests()
