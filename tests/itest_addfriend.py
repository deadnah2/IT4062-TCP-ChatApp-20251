import os
import select
import socket
import subprocess
import sys
import time
from contextlib import closing

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SERVER_BIN = os.path.join(PROJECT_ROOT, "build", "server")


# ---------- Helpers (self-contained copy) ----------

def die(msg: str) -> None:
    print(f"[FAIL] {msg}")
    sys.exit(1)


def ok(msg: str) -> None:
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
    if not os.path.exists(SERVER_BIN):
        die("server binary not found")

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




# ---------- Friend tests ----------

def test_FRIEND_INVITE_success(port: int):
    c = Conn("127.0.0.1", port)

    c.send_line("REGISTER 1 username=alice password=pass1234 email=a@a.com")
    assert c.recv_line().startswith("OK")

    c.send_line("REGISTER 2 username=bob password=pass1234 email=b@b.com")
    assert c.recv_line().startswith("OK")

    c.send_line("LOGIN 3 username=alice password=pass1234")
    token = parse_kv(parse_resp(c.recv_line())[2]).get("token")
    assert token

    c.send_line(f"FRIEND_INVITE 4 token={token} username=bob")
    r = c.recv_line()
    kind, rid, _ = parse_resp(r)
    assert kind == "OK" and rid == "4", r

    c.close()
    ok("add friend: success")


def test_FRIEND_INVITE_duplicate(port: int):
    c = Conn("127.0.0.1", port)

    c.send_line("REGISTER 10 username=dup1 password=pass1234 email=a@a.com")
    c.recv_line()
    c.send_line("REGISTER 11 username=dup2 password=pass1234 email=b@b.com")
    c.recv_line()

    c.send_line("LOGIN 12 username=dup1 password=pass1234")
    r = c.recv_line()
    token = parse_kv(parse_resp(r)[2]).get("token")
    assert token

    c.send_line(f"FRIEND_INVITE 13 token={token} username=dup2")
    r = c.recv_line()
    kind, rid, rest = parse_resp(r)
    assert kind == "OK", r

    c.send_line(f"FRIEND_INVITE 14 token={token} username=dup2")
    r = c.recv_line()
    kind, rid, rest = parse_resp(r)
    assert kind == "ERR" and rest.startswith("409"), r

    c.close()
    ok("friend invite: duplicate")



def test_FRIEND_INVITE_invalid_token(port: int):
    c = Conn("127.0.0.1", port)

    c.send_line("FRIEND_INVITE 20 token=badtoken username=anyone")
    r = c.recv_line()
    kind, rid, rest = parse_resp(r)

    assert kind == "ERR" and rid == "20" and rest.startswith("401"), r

    c.close()
    ok("add friend: invalid token")


def test_FRIEND_INVITE_user_not_found(port: int):
    c = Conn("127.0.0.1", port)

    c.send_line("REGISTER 30 username=nf1 password=pass1234 email=u3@a.com")
    _ = c.recv_line()

    c.send_line("LOGIN 31 username=nf1 password=pass1234")
    token = parse_kv(parse_resp(c.recv_line())[2]).get("token")

    c.send_line(f"FRIEND_INVITE 32 token={token} username=ghost")
    r = c.recv_line()
    kind, rid, rest = parse_resp(r)

    assert kind == "ERR" and rest.startswith("404"), r

    c.close()
    ok("add friend: user not found")


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
    run_one(test_FRIEND_INVITE_success)
    run_one(test_FRIEND_INVITE_duplicate)
    run_one(test_FRIEND_INVITE_invalid_token)
    run_one(test_FRIEND_INVITE_user_not_found)

    print("\nAll friend tests passed.")



if __name__ == "__main__":
    main()
