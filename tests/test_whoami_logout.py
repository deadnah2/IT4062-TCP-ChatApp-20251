import os
import select
import socket
import subprocess
import sys
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

    def send_line(self, line: str):
        self.sock.sendall((line + "\r\n").encode("utf-8"))

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
        die(f"server binary not found: {SERVER_BIN} (run `make` first)")

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


def must_err(resp: str, exp_rid: str, exp_code: str):
    kind, rid, rest = parse_resp(resp)
    if kind != "ERR" or rid != exp_rid:
        die(f"expected ERR rid={exp_rid}, got: {resp}")
    parts = rest.split(" ", 1)
    code = parts[0] if parts else ""
    if code != exp_code:
        die(f"expected ERR {exp_code}, got: {resp}")


def must_ok(resp: str, exp_rid: str):
    kind, rid, _ = parse_resp(resp)
    if kind != "OK" or rid != exp_rid:
        die(f"expected OK rid={exp_rid}, got: {resp}")


def register_user(c: Conn, rid: str, username: str, password: str, email: str) -> int:
    c.send_line(f"REGISTER {rid} username={username} password={password} email={email}")
    resp = c.recv_line()
    must_ok(resp, rid)
    _, _, rest = parse_resp(resp)
    kv = parse_kv(rest)
    if "user_id" not in kv:
        die(f"REGISTER missing user_id: {resp}")
    return int(kv["user_id"])


def login_user(c: Conn, rid: str, username: str, password: str) -> tuple[str, int]:
    c.send_line(f"LOGIN {rid} username={username} password={password}")
    resp = c.recv_line()
    must_ok(resp, rid)
    _, _, rest = parse_resp(resp)
    kv = parse_kv(rest)
    token = kv.get("token", "")
    if not token:
        die(f"LOGIN missing token: {resp}")
    user_id = int(kv.get("user_id", "0") or "0")
    if user_id <= 0:
        die(f"LOGIN missing user_id: {resp}")
    return (token, user_id)


def whoami(c: Conn, rid: str, token_payload: str):
    c.send_line(f"WHOAMI {rid} {token_payload}".strip())
    return c.recv_line()


def logout(c: Conn, rid: str, token_payload: str):
    c.send_line(f"LOGOUT {rid} {token_payload}".strip())
    return c.recv_line()


def test_missing_fields(port: int):
    c = Conn("127.0.0.1", port)

    resp = whoami(c, "1", "")
    must_err(resp, "1", "400")

    resp = logout(c, "2", "")
    must_err(resp, "2", "400")

    resp = whoami(c, "3", "token=")
    must_err(resp, "3", "401")

    resp = logout(c, "4", "token=")
    must_err(resp, "4", "401")

    c.close()
    ok("whoami/logout: missing_fields + empty token")


def test_invalid_token(port: int):
    c = Conn("127.0.0.1", port)

    resp = whoami(c, "1", "token=abc")
    must_err(resp, "1", "401")

    resp = logout(c, "2", "token=abc")
    must_err(resp, "2", "401")

    c.close()
    ok("whoami/logout: invalid token")


def test_happy_path_and_double_logout(port: int):
    c = Conn("127.0.0.1", port)

    username = f"u{int(time.time())}"
    password = "pass1234"
    email = f"{username}@ex.com"
    _uid = register_user(c, "1", username, password, email)
    token, uid = login_user(c, "2", username, password)

    resp = whoami(c, "3", f"token={token}")
    must_ok(resp, "3")
    _, _, rest = parse_resp(resp)
    if parse_kv(rest).get("user_id") != str(uid):
        die(f"WHOAMI returned wrong user_id: {resp}")

    resp = logout(c, "4", f"token={token}")
    must_ok(resp, "4")

    resp = whoami(c, "5", f"token={token}")
    must_err(resp, "5", "401")

    resp = logout(c, "6", f"token={token}")
    must_err(resp, "6", "401")

    c.close()
    ok("whoami/logout: happy path + token invalid after logout")


def test_disconnect_cleanup(port: int):
    c1 = Conn("127.0.0.1", port)

    username = f"u{int(time.time())}_dc"
    password = "pass1234"
    email = f"{username}@ex.com"
    register_user(c1, "1", username, password, email)
    token, _ = login_user(c1, "2", username, password)

    # Close socket without LOGOUT. Server should auto-clean session bound to socket.
    c1.close()
    time.sleep(0.2)

    c2 = Conn("127.0.0.1", port)
    resp = whoami(c2, "3", f"token={token}")
    must_err(resp, "3", "401")
    c2.close()
    ok("sessions: cleanup on disconnect invalidates token")


def test_second_login_same_socket_invalidates_old_token(port: int):
    c = Conn("127.0.0.1", port)

    username = f"u{int(time.time())}_relogin"
    password = "pass1234"
    email = f"{username}@ex.com"
    register_user(c, "1", username, password, email)

    token1, _ = login_user(c, "2", username, password)
    token2, _ = login_user(c, "3", username, password)

    resp = whoami(c, "4", f"token={token1}")
    must_err(resp, "4", "401")

    resp = whoami(c, "5", f"token={token2}")
    must_ok(resp, "5")

    c.close()
    ok("sessions: second login on same socket invalidates old token")


def test_multi_login_block_then_logout_allows_new_login(port: int):
    username = f"u{int(time.time())}_ml"
    password = "pass1234"
    email = f"{username}@ex.com"

    c1 = Conn("127.0.0.1", port)
    register_user(c1, "1", username, password, email)
    token1, _ = login_user(c1, "2", username, password)

    c2 = Conn("127.0.0.1", port)
    c2.send_line(f"LOGIN 3 username={username} password={password}")
    resp = c2.recv_line()
    kind, rid, rest = parse_resp(resp)
    if kind != "ERR" or rid != "3" or not rest.startswith("409"):
        die(f"expected multi-login blocked (409), got: {resp}")

    # Logout on first connection, then login from second should succeed.
    resp = logout(c1, "4", f"token={token1}")
    must_ok(resp, "4")

    token2, _ = login_user(c2, "5", username, password)
    resp = whoami(c2, "6", f"token={token2}")
    must_ok(resp, "6")

    c1.close()
    c2.close()
    ok("sessions: block multi-login, allow after logout")


def test_whoami_refreshes_timeout():
    port = free_port()
    # Dùng timeout lớn hơn để tránh flakiness do time() độ phân giải theo giây.
    proc = start_server(port, timeout_s=4)
    try:
        c = Conn("127.0.0.1", port)
        username = f"u{int(time.time())}_to"
        password = "pass1234"
        email = f"{username}@ex.com"
        register_user(c, "1", username, password, email)
        token, _ = login_user(c, "2", username, password)

        # WHOAMI ngay sau LOGIN phải hợp lệ.
        resp = whoami(c, "3", f"token={token}")
        must_ok(resp, "3")

        # Kiểm tra "refresh": gọi WHOAMI định kỳ để gia hạn last_activity.
        # Nếu sessions_validate() không cập nhật last_activity, call thứ 2/3 sẽ bị expired (timeout=4s).
        time.sleep(2.2)
        resp = whoami(c, "4", f"token={token}")
        must_ok(resp, "4")

        time.sleep(2.2)
        resp = whoami(c, "5", f"token={token}")
        must_ok(resp, "5")

        # Không hoạt động đủ lâu => expired.
        time.sleep(4.2)
        resp = whoami(c, "6", f"token={token}")
        must_err(resp, "6", "401")

        c.close()
        ok("sessions: WHOAMI refreshes timeout + expires after idle")
    finally:
        stop_server(proc)


def main():
    def run():
        port = free_port()
        proc = start_server(port, timeout_s=3600)
        try:
            test_missing_fields(port)
            test_invalid_token(port)
            test_happy_path_and_double_logout(port)
            test_disconnect_cleanup(port)
            test_second_login_same_socket_invalidates_old_token(port)
            test_multi_login_block_then_logout_allows_new_login(port)
        finally:
            stop_server(proc)

        test_whoami_refreshes_timeout()

    with_clean_db(run)
    print("\nAll WHOAMI/LOGOUT tests passed.")


if __name__ == "__main__":
    main()
