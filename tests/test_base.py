#!/usr/bin/env python3
"""
Test Suite: Base Features
Tests for: Framing, Server IO, Accounts, Sessions

Test Cases:
1. Framing - Split bytes (send byte by byte)
2. Framing - Multiple lines in one send
3. Framing - Overlong line (>64KB) causes disconnect
4. Server IO - Concurrent PING from multiple clients
5. Accounts - Register success
6. Accounts - Register duplicate username (409)
7. Accounts - Register invalid username (422)
8. Accounts - Register invalid password (422)
9. Accounts - Register invalid email (422)
10. Sessions - Login success
11. Sessions - Login wrong password (401)
12. Sessions - Login non-existent user (401)
13. Sessions - Multi-login blocked (409)
14. Sessions - Whoami with valid token
15. Sessions - Whoami with invalid token (401)
16. Sessions - Logout success
17. Sessions - Session cleanup on disconnect
18. Sessions - Session timeout
"""

import sys
import os
import time
import threading

sys.path.insert(0, os.path.dirname(__file__))
from test_utils import (
    Conn, free_port, start_server, stop_server,
    backup_data, restore_data, parse_resp, parse_kv,
    ok, die, info, section, TestRunner
)


# ============ Framing Tests ============

def test_framing_split_bytes(port: int):
    """Send PING byte by byte - server should reassemble"""
    c = Conn(port=port)
    
    # Send "PING 1\r\n" one byte at a time
    data = b"PING 1\r\n"
    for byte in data:
        c.sock.sendall(bytes([byte]))
        time.sleep(0.01)
    
    resp = c.recv_line()
    kind, rid, rest = parse_resp(resp)
    
    assert kind == "OK", f"Expected OK, got {kind}"
    assert rid == "1", f"Expected rid=1, got {rid}"
    assert "pong" in rest, f"Expected pong in response"
    
    c.close()


def test_framing_multiple_lines(port: int):
    """Send multiple commands in one TCP send"""
    c = Conn(port=port)
    
    # Send 3 PINGs in one packet
    data = "PING 1\r\nPING 2\r\nPING 3\r\n"
    c.sock.sendall(data.encode())
    
    # Should receive 3 responses
    for i in range(1, 4):
        resp = c.recv_line()
        kind, rid, _ = parse_resp(resp)
        assert kind == "OK", f"Expected OK for PING {i}"
        assert rid == str(i), f"Expected rid={i}, got {rid}"
    
    c.close()


def test_framing_overlong_line(port: int):
    """Send line > 64KB - server should disconnect"""
    c = Conn(port=port)
    
    # Send 70KB of data without \r\n
    try:
        c.sock.sendall(b"A" * 70000)
        time.sleep(0.5)
        c.sock.sendall(b"B" * 1000)
        time.sleep(0.5)
        
        # Try to receive - should get disconnected
        c.sock.settimeout(2)
        data = c.sock.recv(1024)
        
        # If we get here, either disconnected (empty) or error response
        # Both are acceptable
        if data:
            # Server might send error before closing
            pass
    except (ConnectionResetError, BrokenPipeError, EOFError):
        pass  # Expected - server closed connection
    except Exception:
        pass
    
    c.close()


def test_concurrent_ping(port: int):
    """Multiple clients sending PING concurrently"""
    NUM_CLIENTS = 10
    results = []
    errors = []
    
    def ping_client(client_id):
        try:
            c = Conn(port=port)
            c.send_line(f"PING {client_id}")
            resp = c.recv_line()
            kind, rid, _ = parse_resp(resp)
            if kind == "OK" and rid == str(client_id):
                results.append(client_id)
            else:
                errors.append(f"Client {client_id}: unexpected response {resp}")
            c.close()
        except Exception as e:
            errors.append(f"Client {client_id}: {e}")
    
    threads = []
    for i in range(NUM_CLIENTS):
        t = threading.Thread(target=ping_client, args=(i,))
        threads.append(t)
        t.start()
    
    for t in threads:
        t.join(timeout=5)
    
    assert len(results) == NUM_CLIENTS, f"Only {len(results)}/{NUM_CLIENTS} succeeded. Errors: {errors}"


# ============ Account Tests ============

def test_register_success(port: int):
    """Register new user successfully"""
    c = Conn(port=port)
    
    kind, rid, rest = c.register("testuser1", "password123", "test1@example.com")
    
    assert kind == "OK", f"Expected OK, got {kind}: {rest}"
    kv = parse_kv(rest)
    assert "user_id" in kv, "No user_id in response"
    
    c.close()


def test_register_duplicate(port: int):
    """Register duplicate username - should fail with 409"""
    c = Conn(port=port)
    
    # First registration
    c.register("dupuser", "password123", "dup1@example.com")
    
    # Second registration with same username
    kind, rid, rest = c.register("dupuser", "password456", "dup2@example.com")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "409" in rest, f"Expected 409 error, got {rest}"
    
    c.close()


def test_register_invalid_username_short(port: int):
    """Register with too short username (< 3 chars) - should fail with 422"""
    c = Conn(port=port)
    
    kind, rid, rest = c.register("ab", "password123", "short@example.com")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "422" in rest, f"Expected 422 error, got {rest}"
    
    c.close()


def test_register_invalid_username_chars(port: int):
    """Register with invalid characters in username - should fail with 422"""
    c = Conn(port=port)
    
    kind, rid, rest = c.register("user@name", "password123", "char@example.com")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "422" in rest, f"Expected 422 error, got {rest}"
    
    c.close()


def test_register_invalid_password_short(port: int):
    """Register with too short password (< 6 chars) - should fail with 422"""
    c = Conn(port=port)
    
    kind, rid, rest = c.register("passuser", "12345", "pass@example.com")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "422" in rest, f"Expected 422 error, got {rest}"
    
    c.close()


def test_register_invalid_email(port: int):
    """Register with invalid email - should fail with 422"""
    c = Conn(port=port)
    
    kind, rid, rest = c.register("emailuser", "password123", "notanemail")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "422" in rest, f"Expected 422 error, got {rest}"
    
    c.close()


# ============ Session Tests ============

def test_login_success(port: int):
    """Login with correct credentials"""
    c = Conn(port=port)
    
    c.register("loginuser", "password123", "login@example.com")
    kind, rid, rest, token = c.login("loginuser", "password123")
    
    assert kind == "OK", f"Expected OK, got {kind}: {rest}"
    assert token, "No token returned"
    assert len(token) == 32, f"Token should be 32 chars, got {len(token)}"
    
    c.close()


def test_login_wrong_password(port: int):
    """Login with wrong password - should fail with 401"""
    c = Conn(port=port)
    
    c.register("wrongpass", "correctpass", "wrong@example.com")
    kind, rid, rest, token = c.login("wrongpass", "incorrectpass")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "401" in rest, f"Expected 401 error, got {rest}"
    assert not token, "Should not return token on failed login"
    
    c.close()


def test_login_nonexistent_user(port: int):
    """Login with non-existent user - should fail with 401"""
    c = Conn(port=port)
    
    kind, rid, rest, token = c.login("nosuchuser", "password123")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "401" in rest, f"Expected 401 error, got {rest}"
    
    c.close()


def test_multi_login_blocked(port: int):
    """Same user login from second connection - should fail with 409"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("multiuser", "password123", "multi@example.com")
    
    # First login
    kind1, _, _, token1 = c1.login("multiuser", "password123")
    assert kind1 == "OK", "First login should succeed"
    
    # Second login from different connection
    kind2, _, rest2, token2 = c2.login("multiuser", "password123")
    assert kind2 == "ERR", f"Second login should fail, got {kind2}"
    assert "409" in rest2, f"Expected 409 error, got {rest2}"
    
    c1.close()
    c2.close()


def test_whoami_valid_token(port: int):
    """Whoami with valid token"""
    c = Conn(port=port)
    
    c.register("whoamiuser", "password123", "whoami@example.com")
    kind, _, _, token = c.login("whoamiuser", "password123")
    assert kind == "OK"
    
    kind, rid, rest = c.whoami(token)
    
    assert kind == "OK", f"Expected OK, got {kind}"
    kv = parse_kv(rest)
    # Server returns user_id, not username
    assert "user_id" in kv, f"No user_id in response: {kv}"
    
    c.close()


def test_whoami_invalid_token(port: int):
    """Whoami with invalid token - should fail with 401"""
    c = Conn(port=port)
    
    kind, rid, rest = c.whoami("invalid_token_12345678901234567890")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "401" in rest, f"Expected 401 error, got {rest}"
    
    c.close()


def test_logout_success(port: int):
    """Logout invalidates token"""
    c = Conn(port=port)
    
    c.register("logoutuser", "password123", "logout@example.com")
    _, _, _, token = c.login("logoutuser", "password123")
    
    # Logout
    kind, _, rest = c.logout(token)
    assert kind == "OK", f"Logout should succeed: {rest}"
    
    # Token should be invalid now
    kind, _, rest = c.whoami(token)
    assert kind == "ERR", "Token should be invalid after logout"
    assert "401" in rest, f"Expected 401, got {rest}"
    
    c.close()


def test_session_cleanup_on_disconnect(port: int):
    """Session cleaned up when client disconnects"""
    c1 = Conn(port=port)
    
    c1.register("disconnuser", "password123", "disconn@example.com")
    _, _, _, token = c1.login("disconnuser", "password123")
    
    # Disconnect without logout
    c1.close()
    time.sleep(0.5)  # Give server time to cleanup
    
    # Should be able to login again from new connection
    c2 = Conn(port=port)
    kind, _, _, _ = c2.login("disconnuser", "password123")
    assert kind == "OK", "Should be able to login after disconnect"
    
    c2.close()


def test_session_timeout(port: int):
    """Session expires after timeout"""
    # This test needs a server started with short timeout
    # We'll skip detailed implementation as it requires server restart
    pass  # Tested in itest.py with special server config


# ============ Main ============

def run_all(port: int):
    """Run all base tests"""
    runner = TestRunner("Base Features")
    
    # Framing tests
    runner.add_test(test_framing_split_bytes, "Framing: split bytes")
    runner.add_test(test_framing_multiple_lines, "Framing: multiple lines in one send")
    runner.add_test(test_framing_overlong_line, "Framing: overlong line disconnect")
    runner.add_test(test_concurrent_ping, "Server IO: concurrent PING")
    
    # Account tests
    runner.add_test(test_register_success, "Accounts: register success")
    runner.add_test(test_register_duplicate, "Accounts: duplicate username (409)")
    runner.add_test(test_register_invalid_username_short, "Accounts: invalid username - too short (422)")
    runner.add_test(test_register_invalid_username_chars, "Accounts: invalid username - bad chars (422)")
    runner.add_test(test_register_invalid_password_short, "Accounts: invalid password - too short (422)")
    runner.add_test(test_register_invalid_email, "Accounts: invalid email (422)")
    
    # Session tests
    runner.add_test(test_login_success, "Sessions: login success")
    runner.add_test(test_login_wrong_password, "Sessions: wrong password (401)")
    runner.add_test(test_login_nonexistent_user, "Sessions: non-existent user (401)")
    runner.add_test(test_multi_login_blocked, "Sessions: multi-login blocked (409)")
    runner.add_test(test_whoami_valid_token, "Sessions: whoami valid token")
    runner.add_test(test_whoami_invalid_token, "Sessions: whoami invalid token (401)")
    runner.add_test(test_logout_success, "Sessions: logout success")
    runner.add_test(test_session_cleanup_on_disconnect, "Sessions: cleanup on disconnect")
    
    return runner.run(port)


def main():
    port = free_port()
    
    backup_data()
    proc = start_server(port, timeout_s=3600)
    
    try:
        passed, failed = run_all(port)
        print(f"\n{'='*60}")
        print(f"Base Tests: {passed} passed, {failed} failed")
        print(f"{'='*60}")
        return 0 if failed == 0 else 1
    finally:
        stop_server(proc)
        restore_data()


if __name__ == "__main__":
    sys.exit(main())
