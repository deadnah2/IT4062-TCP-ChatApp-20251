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
    return kind, rid, rest


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


def start_server(port: int):
    proc = subprocess.Popen(
        [SERVER_BIN, str(port), "3"],
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

    dbs = ["users.db", "groups.db", "group_members.db", "sessions.db"]
    baks = []

    for f in dbs:
        real = os.path.join(data_dir, f)
        bak = real + ".bak"
        if os.path.exists(bak):
            os.remove(bak)
        if os.path.exists(real):
            os.rename(real, bak)
        baks.append((real, bak))

    try:
        fn()
    finally:
        for real, bak in baks:
            if os.path.exists(real):
                os.remove(real)
            if os.path.exists(bak):
                os.rename(bak, real)


# ---------- Tests ----------

def test_GROUP_ADD_and_MEMBERS(port: int):
    c = Conn("127.0.0.1", port)

    # register users
    c.send_line("REGISTER 1 username=alice password=pass1234 email=a@a.com")
    c.recv_line()

    c.send_line("REGISTER 2 username=bob password=pass1234 email=b@b.com")
    c.recv_line()

    # login alice
    c.send_line("LOGIN 3 username=alice password=pass1234")
    r = c.recv_line()
    token = parse_kv(parse_resp(r)[2]).get("token")
    assert token, "no token"

    # create group
    c.send_line(f"GROUP_CREATE 4 token={token} name=testgroup")
    r = c.recv_line()
    kind, _, rest = parse_resp(r)
    assert kind == "OK", r
    group_id = parse_kv(rest).get("group_id")
    assert group_id, "no group_id"

    # add bob
    c.send_line(f"GROUP_ADD 5 token={token} group_id={group_id} username=bob")
    r = c.recv_line()
    kind, rid, rest = parse_resp(r)
    assert kind == "OK" and rid == "5", r

    # list members
    c.send_line(f"GROUP_MEMBERS 6 token={token} group_id={group_id}")
    r = c.recv_line()
    kind, rid, rest = parse_resp(r)
    assert kind == "OK" and rid == "6", r

    members = parse_kv(rest).get("members", "")
    assert "alice" in members
    assert "bob" in members

    c.close()
    ok("GROUP_ADD + GROUP_MEMBERS")


def test_GROUP_ADD_permission_denied(port: int):
    c = Conn("127.0.0.1", port)

    # users
    c.send_line("REGISTER 10 username=minh password=pass1234 email=minh@a.com")
    c.recv_line()
    c.send_line("REGISTER 11 username=linh password=pass1234 email=linh@a.com")
    c.recv_line()

    # login minh
    c.send_line("LOGIN 12 username=minh password=pass1234")
    token1 = parse_kv(parse_resp(c.recv_line())[2]).get("token")
    # print(token1)
    c.send_line(f"GROUP_CREATE 13 token={token1} name=g1")
    r = c.recv_line()
    # login linh
    c.send_line("LOGIN 14 username=linh password=pass1234")
    token2 = parse_kv(parse_resp(c.recv_line())[2]).get("token")
    # print(token2)
    
    group_id = parse_kv(parse_resp(r)[2]).get("group_id")

    # linh try add member (not owner)
    c.send_line(f"GROUP_ADD 15 token={token2} group_id={group_id} username=minh")
    r = c.recv_line()
    # print(group_id)
    kind, _, rest = parse_resp(r)
    assert kind == "ERR" and rest.startswith("403"), r
    c.close()
    ok("GROUP_ADD permission denied")


# ---------- Runner ----------

def run_one(test_fn):
    port = free_port()

    def run():
        proc = start_server(port)
        try:
            test_fn(port)
        finally:
            stop_server(proc)

    with_clean_db(run)


def main():
    run_one(test_GROUP_ADD_and_MEMBERS)
    run_one(test_GROUP_ADD_permission_denied)
    print("\nAll group tests passed.")


if __name__ == "__main__":
    main()
