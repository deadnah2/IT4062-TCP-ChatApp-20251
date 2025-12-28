import os
import select
import socket
import subprocess
import sys
import time
from contextlib import closing

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SERVER_BIN = os.path.join(PROJECT_ROOT, "build", "server")


# ---------- Helpers ----------

def die(msg: str):
    print(f"[FAIL] {msg}")
    sys.exit(1)


def ok(msg: str):
    print(f"[OK] {msg}")


def free_port() -> int:
    with closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def parse_resp(line: str):
    parts = line.strip().split(" ", 2)
    if len(parts) < 2:
        return ("", "", "")
    kind = parts[0]
    rid = parts[1]
    rest = parts[2] if len(parts) >= 3 else ""
    return (kind, rid, rest)


def parse_kv(payload: str):
    kv = {}
    for token in payload.split():
        if "=" in token:
            k, v = token.split("=", 1)
            kv[k] = v
    return kv


class Conn:
    def __init__(self, host: str, port: int):
        self.sock = socket.create_connection((host, port), timeout=3)
        self.buf = b""

    def send_line(self, line: str):
        self.sock.sendall((line + "\r\n").encode())

    def recv_line(self, timeout: float = 3.0) -> str:
        self.sock.settimeout(timeout)
        while b"\r\n" not in self.buf:
            d = self.sock.recv(1024)
            if not d:
                raise EOFError("disconnected")
            self.buf += d
        line, self.buf = self.buf.split(b"\r\n", 1)
        return line.decode()

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass


def start_server(port: int, timeout_s: int):
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


def with_clean_db(fn):
    data_dir = os.path.join(PROJECT_ROOT, "data")
    os.makedirs(data_dir, exist_ok=True)

    db_files = ["users.db", "friends.db"]
    
    for f in db_files:
        real = os.path.join(data_dir, f)
        bak = real + ".bak"
        if os.path.exists(bak):
            os.remove(bak)
        if os.path.exists(real):
            os.rename(real, bak)

    try:
        fn()
    finally:
        for f in db_files:
            real = os.path.join(data_dir, f)
            bak = real + ".bak"
            if os.path.exists(real):
                os.remove(real)
            if os.path.exists(bak):
                os.rename(bak, real)


# ---------- Tests ----------

def test_friend_list_online_status(port: int):
    """
    Test FRIEND_LIST returns online/offline status correctly.
    
    Flow:
    1. Register alice and bob
    2. Alice sends friend invite to bob
    3. Bob accepts
    4. Both login -> check friend list -> should show "online"
    5. Alice logout -> bob checks -> alice should be "offline"
    """
    
    # Client for alice
    c_alice = Conn("127.0.0.1", port)
    # Client for bob
    c_bob = Conn("127.0.0.1", port)
    
    # 1. Register alice
    c_alice.send_line("REGISTER 1 username=alice password=pass123 email=a@a.com")
    r = c_alice.recv_line()
    kind, rid, rest = parse_resp(r)
    assert kind == "OK" and rid == "1", f"Register alice failed: {r}"
    
    # 2. Register bob
    c_bob.send_line("REGISTER 2 username=bob password=pass123 email=b@b.com")
    r = c_bob.recv_line()
    kind, rid, rest = parse_resp(r)
    assert kind == "OK" and rid == "2", f"Register bob failed: {r}"
    
    # 3. Login alice
    c_alice.send_line("LOGIN 3 username=alice password=pass123")
    r = c_alice.recv_line()
    kind, rid, rest = parse_resp(r)
    assert kind == "OK" and rid == "3", f"Login alice failed: {r}"
    token_alice = parse_kv(rest).get("token")
    assert token_alice, "No token for alice"
    
    # 4. Login bob
    c_bob.send_line("LOGIN 4 username=bob password=pass123")
    r = c_bob.recv_line()
    kind, rid, rest = parse_resp(r)
    assert kind == "OK" and rid == "4", f"Login bob failed: {r}"
    token_bob = parse_kv(rest).get("token")
    assert token_bob, "No token for bob"
    
    # 5. Alice sends friend invite to bob
    c_alice.send_line(f"FRIEND_INVITE 5 token={token_alice} username=bob")
    r = c_alice.recv_line()
    kind, rid, rest = parse_resp(r)
    assert kind == "OK" and rid == "5", f"Friend invite failed: {r}"
    
    # 6. Bob accepts
    c_bob.send_line(f"FRIEND_ACCEPT 6 token={token_bob} username=alice")
    r = c_bob.recv_line()
    kind, rid, rest = parse_resp(r)
    assert kind == "OK" and rid == "6", f"Friend accept failed: {r}"
    
    # 7. Bob checks friend list - alice should be ONLINE
    c_bob.send_line(f"FRIEND_LIST 7 token={token_bob}")
    r = c_bob.recv_line()
    kind, rid, rest = parse_resp(r)
    assert kind == "OK" and rid == "7", f"Friend list failed: {r}"
    
    username_field = parse_kv(rest).get("username", "")
    assert "alice:online" in username_field, f"Expected alice:online, got: {username_field}"
    
    ok("friend list: shows online status when friend is logged in")
    
    # 8. Alice logout
    c_alice.send_line(f"LOGOUT 8 token={token_alice}")
    r = c_alice.recv_line()
    kind, rid, rest = parse_resp(r)
    assert kind == "OK" and rid == "8", f"Logout alice failed: {r}"
    
    # Small delay to ensure session is cleaned up
    time.sleep(0.1)
    
    # 9. Bob checks friend list again - alice should be OFFLINE
    c_bob.send_line(f"FRIEND_LIST 9 token={token_bob}")
    r = c_bob.recv_line()
    kind, rid, rest = parse_resp(r)
    assert kind == "OK" and rid == "9", f"Friend list failed: {r}"
    
    username_field = parse_kv(rest).get("username", "")
    assert "alice:offline" in username_field, f"Expected alice:offline, got: {username_field}"
    
    ok("friend list: shows offline status after friend logout")
    
    c_alice.close()
    c_bob.close()


def test_friend_list_disconnect_offline(port: int):
    """
    Test that friend shows offline when they disconnect without logout.
    """
    
    c_alice = Conn("127.0.0.1", port)
    c_bob = Conn("127.0.0.1", port)
    
    # Register and login
    c_alice.send_line("REGISTER 10 username=charlie password=pass123 email=c@c.com")
    c_alice.recv_line()
    c_bob.send_line("REGISTER 11 username=dave password=pass123 email=d@d.com")
    c_bob.recv_line()
    
    c_alice.send_line("LOGIN 12 username=charlie password=pass123")
    token_charlie = parse_kv(parse_resp(c_alice.recv_line())[2]).get("token")
    
    c_bob.send_line("LOGIN 13 username=dave password=pass123")
    token_dave = parse_kv(parse_resp(c_bob.recv_line())[2]).get("token")
    
    # Make friends
    c_alice.send_line(f"FRIEND_INVITE 14 token={token_charlie} username=dave")
    c_alice.recv_line()
    c_bob.send_line(f"FRIEND_ACCEPT 15 token={token_dave} username=charlie")
    c_bob.recv_line()
    
    # Check charlie is online
    c_bob.send_line(f"FRIEND_LIST 16 token={token_dave}")
    r = c_bob.recv_line()
    username_field = parse_kv(parse_resp(r)[2]).get("username", "")
    assert "charlie:online" in username_field, f"Expected charlie:online, got: {username_field}"
    
    # Charlie disconnects without logout
    c_alice.close()
    time.sleep(0.2)  # Wait for server to cleanup
    
    # Check charlie is now offline
    c_bob.send_line(f"FRIEND_LIST 17 token={token_dave}")
    r = c_bob.recv_line()
    username_field = parse_kv(parse_resp(r)[2]).get("username", "")
    assert "charlie:offline" in username_field, f"Expected charlie:offline after disconnect, got: {username_field}"
    
    ok("friend list: shows offline after friend disconnects")
    
    c_bob.close()


def main():
    port = free_port()
    timeout_s = 3600

    def run_all():
        proc = start_server(port, timeout_s)
        try:
            test_friend_list_online_status(port)
            test_friend_list_disconnect_offline(port)
        finally:
            stop_server(proc)

    with_clean_db(run_all)
    print("\nAll FRIEND_LIST status tests passed.")


if __name__ == "__main__":
    main()
