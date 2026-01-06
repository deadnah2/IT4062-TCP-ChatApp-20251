#!/usr/bin/env python3
"""
Integration tests for DISCONNECT feature.
Tests:
1. DISCONNECT without login (no token)
2. DISCONNECT with valid token (destroys session and closes connection)
3. DISCONNECT invalidates session (can't use token after)
4. Server closes connection after DISCONNECT response
"""

import socket
import subprocess
import time
import sys
import os

HOST = "127.0.0.1"
PORT = 18888

server_proc = None


def start_server():
    global server_proc
    server_proc = subprocess.Popen(
        ["./build/server", str(PORT)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    time.sleep(0.3)


def stop_server():
    global server_proc
    if server_proc:
        server_proc.terminate()
        server_proc.wait()
        server_proc = None


def send_line(sock, line):
    sock.sendall((line + "\r\n").encode())


def recv_line(sock, timeout=2):
    sock.settimeout(timeout)
    data = b""
    try:
        while b"\r\n" not in data:
            chunk = sock.recv(1024)
            if not chunk:
                break
            data += chunk
    except socket.timeout:
        pass
    if b"\r\n" in data:
        return data.split(b"\r\n")[0].decode()
    return data.decode() if data else None


def new_client():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((HOST, PORT))
    return s


def register_user(sock, username, password, email):
    send_line(sock, f"REGISTER r1 username={username} password={password} email={email}")
    return recv_line(sock)


def login_user(sock, username, password):
    send_line(sock, f"LOGIN l1 username={username} password={password}")
    resp = recv_line(sock)
    if resp and resp.startswith("OK"):
        # Parse token
        for part in resp.split():
            if part.startswith("token="):
                return part.split("=", 1)[1]
    return None


def whoami(sock, token):
    send_line(sock, f"WHOAMI w1 token={token}")
    return recv_line(sock)


def disconnect(sock, token=None):
    if token:
        send_line(sock, f"DISCONNECT d1 token={token}")
    else:
        send_line(sock, f"DISCONNECT d1")
    return recv_line(sock)


# ============= TESTS =============

def test_disconnect_without_login():
    """Test 1: DISCONNECT without being logged in should still work (no session to destroy)"""
    print("\n[INFO] Test 1: DISCONNECT without login")
    
    s = new_client()
    
    # DISCONNECT without token
    resp = disconnect(s)
    
    if resp is None:
        print("[FAIL] No response from server")
        s.close()
        return False
    
    if not resp.startswith("OK"):
        print(f"[FAIL] Expected OK, got: {resp}")
        s.close()
        return False
    
    if "disconnected=1" not in resp:
        print(f"[FAIL] Expected disconnected=1 in response: {resp}")
        s.close()
        return False
    
    # Connection should be closed by server
    time.sleep(0.1)
    try:
        s.settimeout(0.5)
        data = s.recv(1024)
        if data:
            print(f"[FAIL] Expected connection closed, but got data: {data}")
            s.close()
            return False
    except (socket.timeout, ConnectionResetError, BrokenPipeError):
        pass  # Expected
    
    print("[OK] DISCONNECT without login works correctly")
    s.close()
    return True


def test_disconnect_with_token():
    """Test 2: DISCONNECT with valid token should destroy session and close connection"""
    print("\n[INFO] Test 2: DISCONNECT with valid token")
    
    s = new_client()
    
    # Register and login
    register_user(s, "distest1", "pass123456", "dis1@test.com")
    token = login_user(s, "distest1", "pass123456")
    
    if not token:
        print("[FAIL] Could not login")
        s.close()
        return False
    
    # DISCONNECT with token
    resp = disconnect(s, token)
    
    if not resp or not resp.startswith("OK"):
        print(f"[FAIL] Expected OK, got: {resp}")
        s.close()
        return False
    
    if "disconnected=1" not in resp:
        print(f"[FAIL] Expected disconnected=1 in response: {resp}")
        s.close()
        return False
    
    print("[OK] DISCONNECT with token returns OK disconnected=1")
    s.close()
    return True


def test_session_invalidated_after_disconnect():
    """Test 3: Session should be invalidated after DISCONNECT (can't use token on new connection)"""
    print("\n[INFO] Test 3: Session invalidated after DISCONNECT")
    
    s1 = new_client()
    
    # Register and login
    register_user(s1, "distest2", "pass123456", "dis2@test.com")
    token = login_user(s1, "distest2", "pass123456")
    
    if not token:
        print("[FAIL] Could not login")
        s1.close()
        return False
    
    # DISCONNECT with token
    resp = disconnect(s1, token)
    if not resp or not resp.startswith("OK"):
        print(f"[FAIL] DISCONNECT failed: {resp}")
        s1.close()
        return False
    
    s1.close()
    time.sleep(0.1)
    
    # New connection - try to use the old token
    s2 = new_client()
    resp = whoami(s2, token)
    
    if resp is None:
        print("[FAIL] No response from WHOAMI")
        s2.close()
        return False
    
    if resp.startswith("OK"):
        print(f"[FAIL] Token should be invalid, but WHOAMI succeeded: {resp}")
        s2.close()
        return False
    
    if not resp.startswith("ERR"):
        print(f"[FAIL] Expected ERR, got: {resp}")
        s2.close()
        return False
    
    print("[OK] Session invalidated after DISCONNECT - token no longer works")
    s2.close()
    return True


def test_connection_closed_after_disconnect():
    """Test 4: Server should close connection after DISCONNECT response"""
    print("\n[INFO] Test 4: Connection closed after DISCONNECT")
    
    s = new_client()
    
    # Register and login
    register_user(s, "distest3", "pass123456", "dis3@test.com")
    token = login_user(s, "distest3", "pass123456")
    
    if not token:
        print("[FAIL] Could not login")
        s.close()
        return False
    
    # DISCONNECT
    resp = disconnect(s, token)
    if not resp:
        print("[FAIL] No DISCONNECT response")
        s.close()
        return False
    
    # Try to send another command - should fail
    time.sleep(0.1)
    try:
        send_line(s, "PING p1")
        s.settimeout(0.5)
        data = s.recv(1024)
        if data:
            print(f"[FAIL] Expected connection closed, but received: {data.decode()}")
            s.close()
            return False
    except (socket.timeout, ConnectionResetError, BrokenPipeError, OSError):
        pass  # Expected - connection was closed
    
    print("[OK] Connection properly closed after DISCONNECT")
    s.close()
    return True


def test_disconnect_vs_logout():
    """Test 5: Compare DISCONNECT vs LOGOUT - LOGOUT keeps connection, DISCONNECT closes it"""
    print("\n[INFO] Test 5: DISCONNECT vs LOGOUT behavior comparison")
    
    # Test LOGOUT - connection should stay open
    s1 = new_client()
    register_user(s1, "distest4", "pass123456", "dis4@test.com")
    token1 = login_user(s1, "distest4", "pass123456")
    
    if not token1:
        print("[FAIL] Could not login for LOGOUT test")
        s1.close()
        return False
    
    # LOGOUT
    send_line(s1, f"LOGOUT lo1 token={token1}")
    resp1 = recv_line(s1)
    
    if not resp1 or not resp1.startswith("OK"):
        print(f"[FAIL] LOGOUT failed: {resp1}")
        s1.close()
        return False
    
    # Connection should still work - send PING
    send_line(s1, "PING p1")
    resp_ping = recv_line(s1)
    
    if not resp_ping or "pong=1" not in resp_ping:
        print(f"[FAIL] PING failed after LOGOUT (connection should stay open): {resp_ping}")
        s1.close()
        return False
    
    print("  LOGOUT: Connection stays open ✓")
    s1.close()
    
    # Test DISCONNECT - connection should close
    s2 = new_client()
    register_user(s2, "distest5", "pass123456", "dis5@test.com")
    token2 = login_user(s2, "distest5", "pass123456")
    
    if not token2:
        print("[FAIL] Could not login for DISCONNECT test")
        s2.close()
        return False
    
    # DISCONNECT
    resp2 = disconnect(s2, token2)
    
    if not resp2 or not resp2.startswith("OK"):
        print(f"[FAIL] DISCONNECT failed: {resp2}")
        s2.close()
        return False
    
    # Connection should be closed - PING should fail
    time.sleep(0.1)
    try:
        send_line(s2, "PING p2")
        s2.settimeout(0.5)
        data = s2.recv(1024)
        if data:
            print(f"[FAIL] DISCONNECT should close connection, but PING worked: {data.decode()}")
            s2.close()
            return False
    except (socket.timeout, ConnectionResetError, BrokenPipeError, OSError):
        pass  # Expected
    
    print("  DISCONNECT: Connection closed ✓")
    print("[OK] DISCONNECT vs LOGOUT behavior confirmed")
    s2.close()
    return True


def run_all_tests():
    print("=" * 50)
    print("    DISCONNECT Feature Integration Tests")
    print("=" * 50)
    
    results = []
    
    results.append(("Test 1: DISCONNECT without login", test_disconnect_without_login()))
    results.append(("Test 2: DISCONNECT with token", test_disconnect_with_token()))
    results.append(("Test 3: Session invalidated", test_session_invalidated_after_disconnect()))
    results.append(("Test 4: Connection closed", test_connection_closed_after_disconnect()))
    results.append(("Test 5: DISCONNECT vs LOGOUT", test_disconnect_vs_logout()))
    
    print("\n" + "=" * 50)
    print("                  RESULTS")
    print("=" * 50)
    
    all_passed = True
    for name, passed in results:
        status = "✅ PASS" if passed else "❌ FAIL"
        print(f"  {status} - {name}")
        if not passed:
            all_passed = False
    
    print("=" * 50)
    if all_passed:
        print("  All DISCONNECT tests PASSED! ✅")
    else:
        print("  Some tests FAILED! ❌")
    print("=" * 50)
    
    return all_passed


if __name__ == "__main__":
    # Clean up data dir
    data_dir = "data"
    if os.path.exists(data_dir):
        for f in os.listdir(data_dir):
            if f.endswith(".db") or f.endswith(".txt"):
                os.remove(os.path.join(data_dir, f))
        # Clean PM directories
        pm_dir = os.path.join(data_dir, "pm")
        if os.path.exists(pm_dir):
            for f in os.listdir(pm_dir):
                os.remove(os.path.join(pm_dir, f))
    
    start_server()
    try:
        success = run_all_tests()
        sys.exit(0 if success else 1)
    finally:
        stop_server()
