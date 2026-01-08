#!/usr/bin/env python3
"""
Test utilities and helpers for ChatProject integration tests.
Provides common functions, Conn class, server management.
"""

import base64
import os
import select
import socket
import subprocess
import sys
import time
from contextlib import closing

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SERVER_BIN = os.path.join(PROJECT_ROOT, "build", "server")
DATA_DIR = os.path.join(PROJECT_ROOT, "data")


# ============ Output Helpers ============

class Colors:
    GREEN = '\033[92m'
    RED = '\033[91m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    RESET = '\033[0m'
    BOLD = '\033[1m'


def die(msg: str) -> None:
    """Print failure message and exit"""
    print(f"{Colors.RED}[FAIL]{Colors.RESET} {msg}")
    sys.exit(1)


def ok(msg: str) -> None:
    """Print success message"""
    print(f"{Colors.GREEN}[OK]{Colors.RESET} {msg}")


def info(msg: str) -> None:
    """Print info message"""
    print(f"{Colors.BLUE}[INFO]{Colors.RESET} {msg}")


def warn(msg: str) -> None:
    """Print warning message"""
    print(f"{Colors.YELLOW}[WARN]{Colors.RESET} {msg}")


def section(msg: str) -> None:
    """Print section header"""
    print(f"\n{Colors.BOLD}{'='*60}{Colors.RESET}")
    print(f"{Colors.BOLD}{msg}{Colors.RESET}")
    print(f"{Colors.BOLD}{'='*60}{Colors.RESET}\n")


# ============ Protocol Helpers ============

def parse_resp(line: str) -> tuple:
    """
    Parse response line: OK/ERR/PUSH req_id payload
    Returns: (kind, req_id, rest)
    """
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
    try:
        return base64.b64decode(b64).decode('utf-8')
    except Exception:
        return ""


# ============ Network Helpers ============

def free_port() -> int:
    """Get a free port"""
    with closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Conn:
    """
    TCP connection wrapper with line-based framing.
    Provides high-level methods for all protocol commands.
    """
    
    def __init__(self, host: str = "127.0.0.1", port: int = 8888):
        self.sock = socket.create_connection((host, port), timeout=5)
        self.buf = b""
        self.req_id = 0
        self.push_queue = []  # Queue for PUSH messages
        self.token = ""  # Store token for convenience

    def send_line(self, line: str):
        """Send a line (auto-appends \\r\\n)"""
        self.sock.sendall((line + "\r\n").encode())

    def send_bytes(self, data: bytes):
        """Send raw bytes"""
        self.sock.sendall(data)

    def recv_line(self, timeout: float = 3.0, skip_push: bool = True) -> str:
        """
        Receive next line.
        If skip_push=True, PUSH messages are queued and skipped.
        """
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
        """
        Try to receive a line, return None if timeout.
        First checks push_queue, then reads from socket.
        """
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
        """Get next request ID"""
        self.req_id += 1
        return str(self.req_id)

    def close(self):
        """Close connection"""
        try:
            self.sock.close()
        except Exception:
            pass

    # ============ Base Commands ============

    def ping(self) -> tuple:
        """PING -> OK pong=1"""
        rid = self.next_id()
        self.send_line(f"PING {rid}")
        resp = self.recv_line()
        return parse_resp(resp)

    def register(self, username: str, password: str, email: str) -> tuple:
        """REGISTER -> OK user_id=..."""
        rid = self.next_id()
        self.send_line(f"REGISTER {rid} username={username} password={password} email={email}")
        resp = self.recv_line()
        return parse_resp(resp)

    def login(self, username: str, password: str) -> tuple:
        """LOGIN -> OK token=... user_id=..."""
        rid = self.next_id()
        self.send_line(f"LOGIN {rid} username={username} password={password}")
        resp = self.recv_line()
        kind, rid, rest = parse_resp(resp)
        kv = parse_kv(rest)
        self.token = kv.get("token", "")
        return (kind, rid, rest, self.token)

    def logout(self, token: str = None) -> tuple:
        """LOGOUT -> OK"""
        rid = self.next_id()
        t = token or self.token
        self.send_line(f"LOGOUT {rid} token={t}")
        resp = self.recv_line()
        if parse_resp(resp)[0] == "OK":
            self.token = ""
        return parse_resp(resp)

    def whoami(self, token: str = None) -> tuple:
        """WHOAMI -> OK username=... user_id=..."""
        rid = self.next_id()
        t = token or self.token
        self.send_line(f"WHOAMI {rid} token={t}")
        resp = self.recv_line()
        return parse_resp(resp)

    def disconnect(self, token: str = None) -> tuple:
        """DISCONNECT -> OK (server closes connection)"""
        rid = self.next_id()
        if token or self.token:
            t = token or self.token
            self.send_line(f"DISCONNECT {rid} token={t}")
        else:
            self.send_line(f"DISCONNECT {rid}")
        resp = self.recv_line()
        return parse_resp(resp)

    # ============ Friend Commands ============

    def friend_invite(self, username: str, token: str = None) -> tuple:
        """FRIEND_INVITE -> OK"""
        rid = self.next_id()
        t = token or self.token
        self.send_line(f"FRIEND_INVITE {rid} token={t} username={username}")
        return parse_resp(self.recv_line())

    def friend_accept(self, username: str, token: str = None) -> tuple:
        """FRIEND_ACCEPT -> OK"""
        rid = self.next_id()
        t = token or self.token
        self.send_line(f"FRIEND_ACCEPT {rid} token={t} username={username}")
        return parse_resp(self.recv_line())

    def friend_reject(self, username: str, token: str = None) -> tuple:
        """FRIEND_REJECT -> OK"""
        rid = self.next_id()
        t = token or self.token
        self.send_line(f"FRIEND_REJECT {rid} token={t} username={username}")
        return parse_resp(self.recv_line())

    def friend_pending(self, token: str = None) -> tuple:
        """FRIEND_PENDING -> OK username=u1,u2,..."""
        rid = self.next_id()
        t = token or self.token
        self.send_line(f"FRIEND_PENDING {rid} token={t}")
        return parse_resp(self.recv_line())

    def friend_list(self, token: str = None) -> tuple:
        """FRIEND_LIST -> OK friends=user1:online,user2:offline,..."""
        rid = self.next_id()
        t = token or self.token
        self.send_line(f"FRIEND_LIST {rid} token={t}")
        return parse_resp(self.recv_line())

    def friend_delete(self, username: str, token: str = None) -> tuple:
        """FRIEND_DELETE -> OK"""
        rid = self.next_id()
        t = token or self.token
        self.send_line(f"FRIEND_DELETE {rid} token={t} username={username}")
        return parse_resp(self.recv_line())

    # ============ Group Commands ============

    def group_create(self, name: str, token: str = None) -> tuple:
        """GROUP_CREATE -> OK group_id=..."""
        rid = self.next_id()
        t = token or self.token
        self.send_line(f"GROUP_CREATE {rid} token={t} name={name}")
        resp = self.recv_line()
        kind, rid, rest = parse_resp(resp)
        kv = parse_kv(rest)
        group_id = int(kv.get("group_id", 0))
        return (kind, rid, rest, group_id)

    def group_add(self, group_id: int, username: str, token: str = None) -> tuple:
        """GROUP_ADD -> OK"""
        rid = self.next_id()
        t = token or self.token
        self.send_line(f"GROUP_ADD {rid} token={t} group_id={group_id} username={username}")
        return parse_resp(self.recv_line())

    def group_remove(self, group_id: int, username: str, token: str = None) -> tuple:
        """GROUP_REMOVE -> OK"""
        rid = self.next_id()
        t = token or self.token
        self.send_line(f"GROUP_REMOVE {rid} token={t} group_id={group_id} username={username}")
        return parse_resp(self.recv_line())

    def group_leave(self, group_id: int, token: str = None) -> tuple:
        """GROUP_LEAVE -> OK"""
        rid = self.next_id()
        t = token or self.token
        self.send_line(f"GROUP_LEAVE {rid} token={t} group_id={group_id}")
        return parse_resp(self.recv_line())

    def group_list(self, token: str = None) -> tuple:
        """GROUP_LIST -> OK groups=id1:name1,id2:name2,..."""
        rid = self.next_id()
        t = token or self.token
        self.send_line(f"GROUP_LIST {rid} token={t}")
        return parse_resp(self.recv_line())

    def group_members(self, group_id: int, token: str = None) -> tuple:
        """GROUP_MEMBERS -> OK members=user1,user2,..."""
        rid = self.next_id()
        t = token or self.token
        self.send_line(f"GROUP_MEMBERS {rid} token={t} group_id={group_id}")
        return parse_resp(self.recv_line())

    # ============ Private Message Commands ============

    def pm_chat_start(self, with_user: str, token: str = None) -> tuple:
        """PM_CHAT_START -> OK"""
        rid = self.next_id()
        t = token or self.token
        self.send_line(f"PM_CHAT_START {rid} token={t} with={with_user}")
        return parse_resp(self.recv_line())

    def pm_chat_end(self, token: str = None) -> tuple:
        """PM_CHAT_END -> OK"""
        rid = self.next_id()
        t = token or self.token
        self.send_line(f"PM_CHAT_END {rid} token={t}")
        return parse_resp(self.recv_line())

    def pm_send(self, to_user: str, content: str, token: str = None) -> tuple:
        """PM_SEND (auto Base64) -> OK msg_id=..."""
        rid = self.next_id()
        t = token or self.token
        content_b64 = b64_encode(content)
        self.send_line(f"PM_SEND {rid} token={t} to={to_user} content={content_b64}")
        return parse_resp(self.recv_line())

    def pm_send_raw(self, to_user: str, content_b64: str, token: str = None) -> tuple:
        """PM_SEND (raw Base64) -> OK msg_id=..."""
        rid = self.next_id()
        t = token or self.token
        self.send_line(f"PM_SEND {rid} token={t} to={to_user} content={content_b64}")
        return parse_resp(self.recv_line())

    def pm_history(self, with_user: str, limit: int = 50, token: str = None) -> tuple:
        """PM_HISTORY -> OK messages=..."""
        rid = self.next_id()
        t = token or self.token
        self.send_line(f"PM_HISTORY {rid} token={t} with={with_user} limit={limit}")
        return parse_resp(self.recv_line())

    def pm_conversations(self, token: str = None) -> tuple:
        """PM_CONVERSATIONS -> OK conversations=..."""
        rid = self.next_id()
        t = token or self.token
        self.send_line(f"PM_CONVERSATIONS {rid} token={t}")
        return parse_resp(self.recv_line())

    # ============ Group Message Commands ============

    def gm_chat_start(self, group_id: int, token: str = None) -> tuple:
        """GM_CHAT_START -> OK"""
        rid = self.next_id()
        t = token or self.token
        self.send_line(f"GM_CHAT_START {rid} token={t} group_id={group_id}")
        return parse_resp(self.recv_line())

    def gm_chat_end(self, token: str = None) -> tuple:
        """GM_CHAT_END -> OK"""
        rid = self.next_id()
        t = token or self.token
        self.send_line(f"GM_CHAT_END {rid} token={t}")
        return parse_resp(self.recv_line())

    def gm_send(self, group_id: int, content: str, token: str = None) -> tuple:
        """GM_SEND (auto Base64) -> OK msg_id=..."""
        rid = self.next_id()
        t = token or self.token
        content_b64 = b64_encode(content)
        self.send_line(f"GM_SEND {rid} token={t} group_id={group_id} content={content_b64}")
        return parse_resp(self.recv_line())

    def gm_send_raw(self, group_id: int, content_b64: str, token: str = None) -> tuple:
        """GM_SEND (raw Base64) -> OK msg_id=..."""
        rid = self.next_id()
        t = token or self.token
        self.send_line(f"GM_SEND {rid} token={t} group_id={group_id} content={content_b64}")
        return parse_resp(self.recv_line())

    def gm_history(self, group_id: int, limit: int = 50, token: str = None) -> tuple:
        """GM_HISTORY -> OK messages=..."""
        rid = self.next_id()
        t = token or self.token
        self.send_line(f"GM_HISTORY {rid} token={t} group_id={group_id} limit={limit}")
        return parse_resp(self.recv_line())


# ============ Server Management ============

def start_server(port: int, timeout_s: int = 3600) -> subprocess.Popen:
    """Start server and wait for it to be ready"""
    if not os.path.exists(SERVER_BIN):
        die(f"Server binary not found: {SERVER_BIN}")

    proc = subprocess.Popen(
        [SERVER_BIN, str(port), str(timeout_s)],
        cwd=PROJECT_ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )

    start = time.time()
    output = []
    while time.time() - start < 3:
        if proc.poll() is not None:
            out = "".join(output) + (proc.stdout.read() if proc.stdout else "")
            die(f"Server exited early:\n{out}")

        if proc.stdout and select.select([proc.stdout], [], [], 0.1)[0]:
            line = proc.stdout.readline()
            if line:
                output.append(line)
                if "Server listening" in line:
                    return proc

    return proc


def stop_server(proc: subprocess.Popen):
    """Stop server gracefully"""
    if proc and proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()


# ============ Database Management ============

DB_FILES = [
    "users.db",
    "friends.db", 
    "groups.db",
    "group_members.db",
]

DB_DIRS = [
    "pm",
    "gm",
]


def backup_data():
    """Backup all data files"""
    os.makedirs(DATA_DIR, exist_ok=True)
    
    # Backup files
    for f in DB_FILES:
        real = os.path.join(DATA_DIR, f)
        bak = real + ".bak"
        if os.path.exists(bak):
            os.remove(bak)
        if os.path.exists(real):
            os.rename(real, bak)
    
    # Backup directories
    import shutil
    for d in DB_DIRS:
        real = os.path.join(DATA_DIR, d)
        bak = real + ".bak"
        if os.path.exists(bak):
            shutil.rmtree(bak)
        if os.path.exists(real):
            os.rename(real, bak)


def restore_data():
    """Restore all data files from backup"""
    import shutil
    
    # Restore files
    for f in DB_FILES:
        real = os.path.join(DATA_DIR, f)
        bak = real + ".bak"
        if os.path.exists(real):
            os.remove(real)
        if os.path.exists(bak):
            os.rename(bak, real)
    
    # Restore directories
    for d in DB_DIRS:
        real = os.path.join(DATA_DIR, d)
        bak = real + ".bak"
        if os.path.exists(real):
            shutil.rmtree(real)
        if os.path.exists(bak):
            os.rename(bak, real)


def clean_data():
    """Remove all test data (without backup)"""
    import shutil
    
    for f in DB_FILES:
        path = os.path.join(DATA_DIR, f)
        if os.path.exists(path):
            os.remove(path)
    
    for d in DB_DIRS:
        path = os.path.join(DATA_DIR, d)
        if os.path.exists(path):
            shutil.rmtree(path)


def with_clean_db(fn):
    """Decorator/wrapper to run test with clean database"""
    backup_data()
    try:
        fn()
    finally:
        restore_data()


# ============ Test Runner ============

class TestRunner:
    """Simple test runner with stats"""
    
    def __init__(self, name: str):
        self.name = name
        self.passed = 0
        self.failed = 0
        self.tests = []
    
    def add_test(self, test_fn, description: str = None):
        """Add a test function"""
        desc = description or test_fn.__name__
        self.tests.append((test_fn, desc))
    
    def run(self, port: int):
        """Run all tests"""
        section(f"Running: {self.name}")
        
        for test_fn, desc in self.tests:
            try:
                test_fn(port)
                self.passed += 1
                ok(desc)
            except AssertionError as e:
                self.failed += 1
                die(f"{desc}: {e}")
            except Exception as e:
                self.failed += 1
                die(f"{desc}: {type(e).__name__}: {e}")
        
        return self.passed, self.failed
    
    def summary(self):
        """Print summary"""
        total = self.passed + self.failed
        print(f"\n{self.name}: {self.passed}/{total} passed")
        return self.failed == 0
