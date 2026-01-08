#!/usr/bin/env python3
"""
Test Suite: Friend Features
Tests for: FRIEND_INVITE, FRIEND_ACCEPT, FRIEND_REJECT, FRIEND_PENDING, FRIEND_LIST, FRIEND_DELETE

Test Cases:
1. FRIEND_INVITE - Send invite successfully
2. FRIEND_INVITE - Invite non-existent user (404)
3. FRIEND_INVITE - Invite self (400)
4. FRIEND_INVITE - Invite already friend (409)
5. FRIEND_INVITE - Invite already pending (409)
6. FRIEND_INVITE - Invalid token (401)
7. FRIEND_PENDING - List pending invites
8. FRIEND_PENDING - Empty list
9. FRIEND_ACCEPT - Accept invite successfully
10. FRIEND_ACCEPT - Accept non-existent invite (404)
11. FRIEND_REJECT - Reject invite successfully
12. FRIEND_REJECT - Reject non-existent invite (404)
13. FRIEND_LIST - List friends with online status
14. FRIEND_LIST - Empty list
15. FRIEND_LIST - Online/offline status correct
16. FRIEND_DELETE - Unfriend successfully
17. FRIEND_DELETE - Unfriend non-friend (404)
18. FRIEND_DELETE - Mutual unfriend (both sides)
"""

import sys
import os
import time

sys.path.insert(0, os.path.dirname(__file__))
from test_utils import (
    Conn, free_port, start_server, stop_server,
    backup_data, restore_data, parse_resp, parse_kv,
    ok, die, info, section, TestRunner
)


# ============ FRIEND_INVITE Tests ============

def test_invite_success(port: int):
    """Send friend invite successfully"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("inviter", "password123", "inviter@test.com")
    c2.register("invitee", "password123", "invitee@test.com")
    
    c1.login("inviter", "password123")
    
    kind, _, rest = c1.friend_invite("invitee")
    
    assert kind == "OK", f"Expected OK, got {kind}: {rest}"
    
    c1.close()
    c2.close()


def test_invite_nonexistent_user(port: int):
    """Invite non-existent user - should fail with 404"""
    c = Conn(port=port)
    
    c.register("lonely", "password123", "lonely@test.com")
    c.login("lonely", "password123")
    
    kind, _, rest = c.friend_invite("nosuchuser")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "404" in rest, f"Expected 404 error, got {rest}"
    
    c.close()


def test_invite_self(port: int):
    """Invite self - should fail with 400 or 422"""
    c = Conn(port=port)
    
    c.register("narcissist", "password123", "narc@test.com")
    c.login("narcissist", "password123")
    
    kind, _, rest = c.friend_invite("narcissist")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    # Server may return 400 or 422 depending on implementation
    assert "400" in rest or "422" in rest, f"Expected 400/422 error, got {rest}"
    
    c.close()


def test_invite_already_friend(port: int):
    """Invite already friend - should fail with 409"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("friend1", "password123", "f1@test.com")
    c2.register("friend2", "password123", "f2@test.com")
    
    c1.login("friend1", "password123")
    c2.login("friend2", "password123")
    
    # Send and accept invite
    c1.friend_invite("friend2")
    c2.friend_accept("friend1")
    
    # Try to invite again
    kind, _, rest = c1.friend_invite("friend2")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "409" in rest, f"Expected 409 error, got {rest}"
    
    c1.close()
    c2.close()


def test_invite_already_pending(port: int):
    """Invite when already pending - should fail with 409"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("pend1", "password123", "p1@test.com")
    c2.register("pend2", "password123", "p2@test.com")
    
    c1.login("pend1", "password123")
    
    # Send first invite
    c1.friend_invite("pend2")
    
    # Try to send again
    kind, _, rest = c1.friend_invite("pend2")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "409" in rest, f"Expected 409 error, got {rest}"
    
    c1.close()
    c2.close()


def test_invite_invalid_token(port: int):
    """Invite with invalid token - should fail with 401"""
    c = Conn(port=port)
    
    c.register("tokuser", "password123", "tok@test.com")
    
    kind, _, rest = c.friend_invite("someone", token="invalid_token_123456789012345")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "401" in rest, f"Expected 401 error, got {rest}"
    
    c.close()


# ============ FRIEND_PENDING Tests ============

def test_pending_list(port: int):
    """List pending invites"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("sender1", "password123", "s1@test.com")
    c2.register("receiver1", "password123", "r1@test.com")
    
    c1.login("sender1", "password123")
    c2.login("receiver1", "password123")
    
    # sender1 invites receiver1
    c1.friend_invite("receiver1")
    
    # receiver1 checks pending
    kind, _, rest = c2.friend_pending()
    
    assert kind == "OK", f"Expected OK, got {kind}"
    kv = parse_kv(rest)
    assert "sender1" in kv.get("username", ""), f"sender1 should be in pending: {rest}"
    
    c1.close()
    c2.close()


def test_pending_empty(port: int):
    """Pending list is empty"""
    c = Conn(port=port)
    
    c.register("nopending", "password123", "np@test.com")
    c.login("nopending", "password123")
    
    kind, _, rest = c.friend_pending()
    
    assert kind == "OK", f"Expected OK, got {kind}"
    kv = parse_kv(rest)
    # Empty or "username=" with no value
    assert kv.get("username", "") == "", f"Should be empty: {rest}"
    
    c.close()


# ============ FRIEND_ACCEPT Tests ============

def test_accept_success(port: int):
    """Accept friend invite successfully"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("acc_s", "password123", "acc_s@test.com")
    c2.register("acc_r", "password123", "acc_r@test.com")
    
    c1.login("acc_s", "password123")
    c2.login("acc_r", "password123")
    
    c1.friend_invite("acc_r")
    
    kind, _, rest = c2.friend_accept("acc_s")
    
    assert kind == "OK", f"Expected OK, got {kind}: {rest}"
    
    # Verify friendship - both should see each other in friend list
    _, _, rest1 = c1.friend_list()
    _, _, rest2 = c2.friend_list()
    
    assert "acc_r" in rest1, f"acc_r should be in acc_s's friend list: {rest1}"
    assert "acc_s" in rest2, f"acc_s should be in acc_r's friend list: {rest2}"
    
    c1.close()
    c2.close()


def test_accept_nonexistent(port: int):
    """Accept non-existent invite - should fail with 404"""
    c = Conn(port=port)
    
    c.register("acc_fail", "password123", "accf@test.com")
    c.login("acc_fail", "password123")
    
    kind, _, rest = c.friend_accept("nosuchuser")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "404" in rest, f"Expected 404 error, got {rest}"
    
    c.close()


# ============ FRIEND_REJECT Tests ============

def test_reject_success(port: int):
    """Reject friend invite successfully"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("rej_s", "password123", "rej_s@test.com")
    c2.register("rej_r", "password123", "rej_r@test.com")
    
    c1.login("rej_s", "password123")
    c2.login("rej_r", "password123")
    
    c1.friend_invite("rej_r")
    
    kind, _, rest = c2.friend_reject("rej_s")
    
    assert kind == "OK", f"Expected OK, got {kind}: {rest}"
    
    # Verify not friends
    _, _, rest1 = c1.friend_list()
    assert "rej_r" not in rest1, f"rej_r should NOT be in friend list: {rest1}"
    
    # Verify pending is empty
    _, _, rest2 = c2.friend_pending()
    assert "rej_s" not in rest2, f"rej_s should NOT be in pending: {rest2}"
    
    c1.close()
    c2.close()


def test_reject_nonexistent(port: int):
    """Reject non-existent invite - should fail with 404"""
    c = Conn(port=port)
    
    c.register("rej_fail", "password123", "rejf@test.com")
    c.login("rej_fail", "password123")
    
    kind, _, rest = c.friend_reject("nosuchuser")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "404" in rest, f"Expected 404 error, got {rest}"
    
    c.close()


# ============ FRIEND_LIST Tests ============

def test_list_with_status(port: int):
    """List friends with online/offline status"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("list_a", "password123", "la@test.com")
    c2.register("list_b", "password123", "lb@test.com")
    
    c1.login("list_a", "password123")
    c2.login("list_b", "password123")
    
    # Make friends
    c1.friend_invite("list_b")
    c2.friend_accept("list_a")
    
    # Both online - check status
    kind, _, rest = c1.friend_list()
    
    assert kind == "OK", f"Expected OK, got {kind}"
    assert "list_b" in rest, f"list_b should be in list: {rest}"
    assert "online" in rest.lower(), f"Should show online status: {rest}"
    
    c1.close()
    c2.close()


def test_list_empty(port: int):
    """Friend list is empty"""
    c = Conn(port=port)
    
    c.register("nofriends", "password123", "nf@test.com")
    c.login("nofriends", "password123")
    
    kind, _, rest = c.friend_list()
    
    assert kind == "OK", f"Expected OK, got {kind}"
    # Empty or "friends=" with no value
    kv = parse_kv(rest)
    friends = kv.get("friends", "")
    assert friends == "" or friends == "empty", f"Should be empty: {rest}"
    
    c.close()


def test_list_online_offline(port: int):
    """Correct online/offline status"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("stat_a", "password123", "sta@test.com")
    c2.register("stat_b", "password123", "stb@test.com")
    
    c1.login("stat_a", "password123")
    c2.login("stat_b", "password123")
    
    # Make friends
    c1.friend_invite("stat_b")
    c2.friend_accept("stat_a")
    
    # stat_b goes offline
    c2.logout()
    c2.close()
    time.sleep(0.3)
    
    # Check stat_b is offline
    kind, _, rest = c1.friend_list()
    
    assert kind == "OK"
    # stat_b should show offline
    assert "offline" in rest.lower(), f"stat_b should be offline: {rest}"
    
    c1.close()


# ============ FRIEND_DELETE Tests ============

def test_delete_success(port: int):
    """Unfriend successfully"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("del_a", "password123", "da@test.com")
    c2.register("del_b", "password123", "db@test.com")
    
    c1.login("del_a", "password123")
    c2.login("del_b", "password123")
    
    # Make friends
    c1.friend_invite("del_b")
    c2.friend_accept("del_a")
    
    # Unfriend
    kind, _, rest = c1.friend_delete("del_b")
    
    assert kind == "OK", f"Expected OK, got {kind}: {rest}"
    
    # Verify not friends anymore
    _, _, rest1 = c1.friend_list()
    _, _, rest2 = c2.friend_list()
    
    assert "del_b" not in rest1, f"del_b should NOT be in list: {rest1}"
    assert "del_a" not in rest2, f"del_a should NOT be in list: {rest2}"
    
    c1.close()
    c2.close()


def test_delete_nonfriend(port: int):
    """Unfriend non-friend - should fail with 404"""
    c = Conn(port=port)
    
    c.register("del_solo", "password123", "ds@test.com")
    c.login("del_solo", "password123")
    
    kind, _, rest = c.friend_delete("nosuchfriend")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "404" in rest, f"Expected 404 error, got {rest}"
    
    c.close()


def test_delete_mutual(port: int):
    """Unfriend removes from both sides"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("mut_a", "password123", "ma@test.com")
    c2.register("mut_b", "password123", "mb@test.com")
    
    c1.login("mut_a", "password123")
    c2.login("mut_b", "password123")
    
    # Make friends
    c1.friend_invite("mut_b")
    c2.friend_accept("mut_a")
    
    # A unfriends B
    c1.friend_delete("mut_b")
    
    # B should not have A in list either
    _, _, rest = c2.friend_list()
    assert "mut_a" not in rest, f"mut_a should NOT be in B's list: {rest}"
    
    # B cannot unfriend A again (already unfriended)
    kind, _, rest = c2.friend_delete("mut_a")
    assert kind == "ERR", "Should fail - already unfriended"
    
    c1.close()
    c2.close()


# ============ Main ============

def run_all(port: int):
    """Run all friend tests"""
    runner = TestRunner("Friend Features")
    
    # FRIEND_INVITE
    runner.add_test(test_invite_success, "FRIEND_INVITE: success")
    runner.add_test(test_invite_nonexistent_user, "FRIEND_INVITE: non-existent user (404)")
    runner.add_test(test_invite_self, "FRIEND_INVITE: invite self (400)")
    runner.add_test(test_invite_already_friend, "FRIEND_INVITE: already friend (409)")
    runner.add_test(test_invite_already_pending, "FRIEND_INVITE: already pending (409)")
    runner.add_test(test_invite_invalid_token, "FRIEND_INVITE: invalid token (401)")
    
    # FRIEND_PENDING
    runner.add_test(test_pending_list, "FRIEND_PENDING: list invites")
    runner.add_test(test_pending_empty, "FRIEND_PENDING: empty list")
    
    # FRIEND_ACCEPT
    runner.add_test(test_accept_success, "FRIEND_ACCEPT: success")
    runner.add_test(test_accept_nonexistent, "FRIEND_ACCEPT: non-existent invite (404)")
    
    # FRIEND_REJECT
    runner.add_test(test_reject_success, "FRIEND_REJECT: success")
    runner.add_test(test_reject_nonexistent, "FRIEND_REJECT: non-existent invite (404)")
    
    # FRIEND_LIST
    runner.add_test(test_list_with_status, "FRIEND_LIST: with online status")
    runner.add_test(test_list_empty, "FRIEND_LIST: empty")
    runner.add_test(test_list_online_offline, "FRIEND_LIST: online/offline status")
    
    # FRIEND_DELETE
    runner.add_test(test_delete_success, "FRIEND_DELETE: success")
    runner.add_test(test_delete_nonfriend, "FRIEND_DELETE: non-friend (404)")
    runner.add_test(test_delete_mutual, "FRIEND_DELETE: mutual removal")
    
    return runner.run(port)


def main():
    port = free_port()
    
    backup_data()
    proc = start_server(port, timeout_s=3600)
    
    try:
        passed, failed = run_all(port)
        print(f"\n{'='*60}")
        print(f"Friend Tests: {passed} passed, {failed} failed")
        print(f"{'='*60}")
        return 0 if failed == 0 else 1
    finally:
        stop_server(proc)
        restore_data()


if __name__ == "__main__":
    sys.exit(main())
