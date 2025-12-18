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

    def send_line(self, line: str, chunk: int | None = None):
        data = (line + "\r\n").encode("utf-8")
        if chunk is None:
            self.sock.sendall(data)
            return
        i = 0
        while i < len(data):
            self.sock.sendall(data[i : i + chunk])
            i += chunk

    def recv_line(self, timeout: float = 3.0) -> str:
        self.sock.settimeout(timeout)
        while b"\r\n" not in self.buf:
            d = self.sock.recv(1024)
            if not d:
                raise EOFError("disconnected")
            self.buf += d
            if len(self.buf) > 256 * 1024:
                raise RuntimeError("buffer too large")
        line, self.buf = self.buf.split(b"\r\n", 1)
        return line.decode("utf-8", errors="replace")

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass


def start_server(port: int, timeout_s: int):
    if not os.path.exists(SERVER_BIN):
        die(f"server binary not found: {SERVER_BIN}")

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
            die(f"server exited early\n{out}")

        if proc.stdout and select.select([proc.stdout], [], [], 0.1)[0]:
            line = proc.stdout.readline()
            if line:
                output.append(line)
                if "Server listening" in line:
                    return proc

    # If we didn't see the line, continue anyway; tests will fail fast on connect.
    return proc


def stop_server(proc: subprocess.Popen):
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()


def with_clean_db(fn):
    data_dir = os.path.join(PROJECT_ROOT, "data")
    os.makedirs(data_dir, exist_ok=True)
    db_path = os.path.join(data_dir, "users.db")
    bak_path = os.path.join(data_dir, "users.db.bak")

    had_db = os.path.exists(db_path)
    if had_db:
        if os.path.exists(bak_path):
            os.remove(bak_path)
        os.rename(db_path, bak_path)

    try:
        fn()
    finally:
        if os.path.exists(db_path):
            os.remove(db_path)
        if had_db and os.path.exists(bak_path):
            os.rename(bak_path, db_path)


def test_framing_split_and_multi(port: int):
    c = Conn("127.0.0.1", port)

    c.send_line("PING 1", chunk=1)
    resp = c.recv_line()
    kind, rid, rest = parse_resp(resp)
    assert kind == "OK" and rid == "1", resp

    # Multiple lines in one send
    c.sock.sendall(b"PING 2\r\nPING 3\r\n")
    resp2 = c.recv_line()
    resp3 = c.recv_line()
    assert resp2.startswith("OK 2")
    assert resp3.startswith("OK 3")

    c.close()
    ok("framing: split bytes + multiple lines")


def test_register_login_logout_whoami(port: int):
    c = Conn("127.0.0.1", port)

    c.send_line("REGISTER 10 username=alice_1 password=pass1234 email=a@b.com")
    r = c.recv_line()
    kind, rid, rest = parse_resp(r)
    assert kind == "OK" and rid == "10", r
    user_id = parse_kv(rest).get("user_id")
    assert user_id is not None

    c.send_line("REGISTER 11 username=alice_1 password=pass1234 email=a@b.com")
    r = c.recv_line()
    kind, rid, rest = parse_resp(r)
    assert kind == "ERR" and rid == "11" and rest.startswith("409 "), r

    c.send_line("REGISTER 12 username=bad! password=pass1234 email=a@b.com")
    r = c.recv_line()
    kind, rid, rest = parse_resp(r)
    assert kind == "ERR" and rid == "12" and rest.startswith("422 "), r

    c.send_line("REGISTER 13 username=bob_1 password=pass1234 email=bad")
    r = c.recv_line()
    kind, rid, rest = parse_resp(r)
    assert kind == "ERR" and rid == "13" and rest.startswith("422 "), r

    c.send_line("LOGIN 20 username=alice_1 password=pass1234")
    r = c.recv_line()
    kind, rid, rest = parse_resp(r)
    assert kind == "OK" and rid == "20", r
    kv = parse_kv(rest)
    token = kv.get("token")
    assert token and len(token) >= 10

    c.send_line(f"WHOAMI 21 token={token}")
    r = c.recv_line()
    kind, rid, rest = parse_resp(r)
    assert kind == "OK" and rid == "21", r

    c.send_line(f"LOGOUT 22 token={token}")
    r = c.recv_line()
    kind, rid, rest = parse_resp(r)
    assert kind == "OK" and rid == "22", r

    c.send_line(f"WHOAMI 23 token={token}")
    r = c.recv_line()
    kind, rid, rest = parse_resp(r)
    assert kind == "ERR" and rid == "23" and rest.startswith("401 "), r

    c.close()
    ok("accounts+sessions: register/login/whoami/logout")


def test_multi_login_and_disconnect_cleanup(port: int):
    c1 = Conn("127.0.0.1", port)
    c2 = Conn("127.0.0.1", port)

    c1.send_line("REGISTER 30 username=u2_1 password=pass1234 email=u2@b.com")
    _ = c1.recv_line()

    c1.send_line("LOGIN 31 username=u2_1 password=pass1234")
    r = c1.recv_line()
    kind, rid, rest = parse_resp(r)
    assert kind == "OK" and rid == "31", r

    c2.send_line("LOGIN 32 username=u2_1 password=pass1234")
    r = c2.recv_line()
    kind, rid, rest = parse_resp(r)
    assert kind == "ERR" and rid == "32" and rest.startswith("409 "), r

    # Close c1 without logout -> server should remove session by socket
    c1.close()
    time.sleep(0.2)

    c2.send_line("LOGIN 33 username=u2_1 password=pass1234")
    r = c2.recv_line()
    kind, rid, rest = parse_resp(r)
    assert kind == "OK" and rid == "33", r

    c2.close()
    ok("sessions: prevent multi-login + cleanup on disconnect")


def test_session_timeout(port: int):
    c = Conn("127.0.0.1", port)

    c.send_line("REGISTER 40 username=u3_1 password=pass1234 email=u3@b.com")
    _ = c.recv_line()

    c.send_line("LOGIN 41 username=u3_1 password=pass1234")
    r = c.recv_line()
    token = parse_kv(parse_resp(r)[2]).get("token")
    assert token

    time.sleep(2.2)

    c.send_line(f"WHOAMI 42 token={token}")
    r = c.recv_line()
    kind, rid, rest = parse_resp(r)
    assert kind == "ERR" and rid == "42" and rest.startswith("401 "), r

    c.close()
    ok("sessions: timeout")


def test_line_too_long_disconnect(port: int):
    c = Conn("127.0.0.1", port)

    big = "PING 50 " + ("a" * 70000)
    c.send_line(big)

    try:
        _ = c.recv_line(timeout=1.0)
        die("expected disconnect on overlong line")
    except Exception:
        pass

    c.close()
    ok("framing: overlong line disconnect")


def test_concurrent_pings(port: int):
    errors = []

    def worker(i: int):
        try:
            c = Conn("127.0.0.1", port)
            c.send_line(f"PING {1000 + i}")
            r = c.recv_line()
            if not r.startswith(f"OK {1000 + i}"):
                errors.append(r)
            c.close()
        except Exception as e:
            errors.append(str(e))

    threads = [threading.Thread(target=worker, args=(i,)) for i in range(30)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()

    if errors:
        die(f"concurrent pings errors: {errors[:3]}")

    ok("server: concurrent clients")


def main():
    port = free_port()
    timeout_s = 2

    def run_all():
        proc = start_server(port, timeout_s)
        try:
            test_framing_split_and_multi(port)
            test_register_login_logout_whoami(port)
            test_multi_login_and_disconnect_cleanup(port)
            test_session_timeout(port)
            test_line_too_long_disconnect(port)
            test_concurrent_pings(port)
        finally:
            stop_server(proc)

    with_clean_db(run_all)
    print("\nAll tests passed.")


if __name__ == "__main__":
    main()

