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

    db_files = [
        "users.db",
        "friends.db",
        "sessions.db",
    ]

    bak_files = []

    for f in db_files:
        real = os.path.join(data_dir, f)
        bak = real + ".bak"

        if os.path.exists(bak):
            os.remove(bak)

        if os.path.exists(real):
            os.rename(real, bak)

        bak_files.append(bak)

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

def setup_two_friends(c: Conn):
    c.send_line("REGISTER 1 username=alice password=pass123 email=a@a.com")
    c.recv_line()
    c.send_line("REGISTER 2 username=bob password=pass123 email=b@b.com")
    c.recv_line()

    c.send_line("LOGIN 3 username=alice password=pass123")
    token_alice = parse_kv(parse_resp(c.recv_line())[2])['token']
    # print(token_alice)

    c.send_line(f"FRIEND_INVITE 4 token={token_alice} username=bob")
    c.recv_line()

    c.send_line("LOGIN 5 username=bob password=pass123")
    token_bob = parse_kv(parse_resp(c.recv_line())[2])['token']

    c.send_line(f"FRIEND_ACCEPT 6 token={token_bob} username=alice")
    print(c.recv_line())
    return token_alice, token_bob


def test_FRIEND_DELETE_success(port: int):
    c = Conn("127.0.0.1", port)

    token_alice, token_bob = setup_two_friends(c)

    c.send_line(f"FRIEND_DELETE 10 token={token_bob} username=alice")
    kind, rid, rest = parse_resp(c.recv_line())

    assert kind == "OK"
    assert "status=deleted" in rest

    c.close()
    ok("friend delete: success")


def test_FRIEND_DELETE_not_found(port: int):
    c = Conn("127.0.0.1", port)

    c.send_line("REGISTER 1 username=u123 password=pass123 email=a@a.com")
    c.recv_line()
    c.send_line("LOGIN 2 username=u123 password=pass123")
    token = parse_kv(parse_resp(c.recv_line())[2])['token']

    c.send_line(f"FRIEND_DELETE 3 token={token} username=ghost")
    kind, rid, rest = parse_resp(c.recv_line())

    assert kind == "ERR"
    assert rest.startswith("404")

    c.close()
    ok("friend delete: not found")


def test_FRIEND_DELETE_invalid_token(port: int):
    c = Conn("127.0.0.1", port)

    c.send_line("FRIEND_DELETE 1 token=badtoken username=any")
    kind, rid, rest = parse_resp(c.recv_line())

    assert kind == "ERR"
    assert rest.startswith("401")

    c.close()
    ok("friend delete: invalid token")


# ---------- Runner ----------

def run_one(test_fn):
    port = free_port()
    timeout_s = 3

    def run():
        proc = start_server(port, timeout_s)
        try:
            test_fn(port)
        finally:
            stop_server(proc)

    with_clean_db(run)


def main():
    run_one(test_FRIEND_DELETE_success)
    run_one(test_FRIEND_DELETE_not_found)
    run_one(test_FRIEND_DELETE_invalid_token)

    print("\nAll FRIEND_DELETE tests passed.")


if __name__ == "__main__":
    main()
