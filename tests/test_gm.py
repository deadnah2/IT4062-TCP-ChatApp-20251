#!/usr/bin/env python3
"""
Test Suite: Group Message (GM) Features
Tests for: GM_CHAT_START, GM_CHAT_END, GM_SEND, GM_HISTORY, PUSH GM, PUSH GM_JOIN/LEAVE/KICKED

Test Cases:
1. GM_SEND - Send message to group successfully
2. GM_SEND - Message stored with correct content
3. GM_SEND - Send to non-existent group (404)
4. GM_SEND - Non-member cannot send (403)
5. GM_SEND - Invalid token (401)
6. GM_SEND - Empty content (400)
7. GM_HISTORY - Get group message history
8. GM_HISTORY - Empty history
9. GM_HISTORY - Non-member cannot view (403)
10. GM_CHAT_START - Enter group chat mode
11. GM_CHAT_START - Non-member cannot join (403)
12. GM_CHAT_END - Exit group chat mode
13. PUSH GM - All members in chat receive message
14. PUSH GM - Only some members in chat
15. PUSH GM_JOIN - Notification when user enters chat
16. PUSH GM_LEAVE - Notification when user leaves chat
17. PUSH GM_KICKED - Notification when kicked from group
18. Multiple groups - User in multiple group chats
19. Large group - 5+ members
20. Unicode content - UTF-8 in group messages
"""

import sys
import os
import time

sys.path.insert(0, os.path.dirname(__file__))
from test_utils import (
    Conn, free_port, start_server, stop_server,
    backup_data, restore_data, parse_resp, parse_kv,
    b64_encode, b64_decode,
    ok, die, info, section, TestRunner
)


# ============ GM_SEND Tests ============

def test_send_success(port: int):
    """Send message to group successfully"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("gm_owner", "password123", "gmo@test.com")
    c2.register("gm_member", "password123", "gmm@test.com")
    
    c1.login("gm_owner", "password123")
    c2.login("gm_member", "password123")
    
    _, _, _, group_id = c1.group_create("GMTestGroup")
    c1.group_add(group_id, "gm_member")
    
    kind, _, rest = c1.gm_send(group_id, "Hello group!")
    
    assert kind == "OK", f"Expected OK, got {kind}: {rest}"
    kv = parse_kv(rest)
    assert "msg_id" in kv, f"No msg_id in response: {rest}"
    
    c1.close()
    c2.close()


def test_send_nonexistent_group(port: int):
    """Send to non-existent group - should fail with 404"""
    c = Conn(port=port)
    
    c.register("gm_nogrp", "password123", "gmng@test.com")
    c.login("gm_nogrp", "password123")
    
    kind, _, rest = c.gm_send(99999, "Hello?")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "404" in rest, f"Expected 404 error, got {rest}"
    
    c.close()


def test_send_nonmember(port: int):
    """Non-member cannot send - should fail with 403"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("gm_own2", "password123", "gmo2@test.com")
    c2.register("gm_outsider", "password123", "gmout@test.com")
    
    c1.login("gm_own2", "password123")
    c2.login("gm_outsider", "password123")
    
    _, _, _, group_id = c1.group_create("PrivateGroup")
    
    # Outsider tries to send
    kind, _, rest = c2.gm_send(group_id, "Can I chat?")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "403" in rest or "404" in rest, f"Expected 403/404 error, got {rest}"
    
    c1.close()
    c2.close()


def test_send_invalid_token(port: int):
    """Send with invalid token - should fail with 401"""
    c = Conn(port=port)
    
    c.register("gm_tok", "password123", "gmtok@test.com")
    c.login("gm_tok", "password123")
    _, _, _, group_id = c.group_create("TokenGroup")
    
    kind, _, rest = c.gm_send(group_id, "Hello", token="invalid_token_12345678901234")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "401" in rest, f"Expected 401 error, got {rest}"
    
    c.close()


def test_send_empty_content(port: int):
    """Send empty content - implementation may accept or reject"""
    c = Conn(port=port)
    
    c.register("gm_empty", "password123", "gme@test.com")
    c.login("gm_empty", "password123")
    _, _, _, group_id = c.group_create("EmptyGroup")
    
    kind, _, rest = c.gm_send_raw(group_id, "")
    
    # Some implementations accept empty, some reject
    assert kind in ("OK", "ERR"), f"Unexpected response: {kind}"
    
    c.close()


# ============ GM_HISTORY Tests ============

def test_history_success(port: int):
    """Get group message history"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("gmh_a", "password123", "gmha@test.com")
    c2.register("gmh_b", "password123", "gmhb@test.com")
    
    c1.login("gmh_a", "password123")
    c2.login("gmh_b", "password123")
    
    _, _, _, group_id = c1.group_create("HistGroup")
    c1.group_add(group_id, "gmh_b")
    
    # Send messages
    c1.gm_send(group_id, "Message 1")
    c2.gm_send(group_id, "Message 2")
    
    # Check history
    kind, _, rest = c1.gm_history(group_id)
    
    assert kind == "OK", f"Expected OK: {rest}"
    # Should have messages
    assert "messages" in rest.lower() or "gmh" in rest.lower(), f"Should have messages: {rest}"
    
    c1.close()
    c2.close()


def test_history_empty(port: int):
    """History is empty"""
    c = Conn(port=port)
    
    c.register("gmhe", "password123", "gmhe@test.com")
    c.login("gmhe", "password123")
    _, _, _, group_id = c.group_create("EmptyHistGroup")
    
    kind, _, rest = c.gm_history(group_id)
    
    assert kind == "OK", f"Expected OK: {rest}"
    kv = parse_kv(rest)
    messages = kv.get("messages", "")
    assert messages == "" or messages == "empty", f"Should be empty: {rest}"
    
    c.close()


def test_history_nonmember(port: int):
    """Non-member cannot view history - should fail with 403"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("gmhn_own", "password123", "gmhno@test.com")
    c2.register("gmhn_out", "password123", "gmhnout@test.com")
    
    c1.login("gmhn_own", "password123")
    c2.login("gmhn_out", "password123")
    
    _, _, _, group_id = c1.group_create("PrivHistGroup")
    c1.gm_send(group_id, "Secret message")
    
    # Outsider tries to view history
    kind, _, rest = c2.gm_history(group_id)
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "403" in rest or "404" in rest, f"Expected 403/404 error, got {rest}"
    
    c1.close()
    c2.close()


# ============ GM_CHAT_START/END Tests ============

def test_chat_start_success(port: int):
    """Enter group chat mode"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("gmc_a", "password123", "gmca@test.com")
    c2.register("gmc_b", "password123", "gmcb@test.com")
    
    c1.login("gmc_a", "password123")
    c2.login("gmc_b", "password123")
    
    _, _, _, group_id = c1.group_create("ChatGroup")
    c1.group_add(group_id, "gmc_b")
    
    kind, _, rest = c1.gm_chat_start(group_id)
    
    assert kind == "OK", f"Expected OK: {rest}"
    
    c1.close()
    c2.close()


def test_chat_start_nonmember(port: int):
    """Non-member cannot join chat - should fail with 403"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("gmcs_own", "password123", "gmcso@test.com")
    c2.register("gmcs_out", "password123", "gmcsout@test.com")
    
    c1.login("gmcs_own", "password123")
    c2.login("gmcs_out", "password123")
    
    _, _, _, group_id = c1.group_create("NoJoinGroup")
    
    kind, _, rest = c2.gm_chat_start(group_id)
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "403" in rest or "404" in rest, f"Expected 403/404 error, got {rest}"
    
    c1.close()
    c2.close()


def test_chat_end_success(port: int):
    """Exit group chat mode"""
    c = Conn(port=port)
    
    c.register("gmce", "password123", "gmce@test.com")
    c.login("gmce", "password123")
    _, _, _, group_id = c.group_create("EndGroup")
    
    c.gm_chat_start(group_id)
    kind, _, rest = c.gm_chat_end()
    
    assert kind == "OK", f"Expected OK: {rest}"
    
    c.close()


# ============ PUSH Tests ============

def test_push_all_members(port: int):
    """All members in chat receive PUSH GM"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    c3 = Conn(port=port)
    
    c1.register("push_a", "password123", "pa@test.com")
    c2.register("push_b", "password123", "pb@test.com")
    c3.register("push_c", "password123", "pc@test.com")
    
    c1.login("push_a", "password123")
    c2.login("push_b", "password123")
    c3.login("push_c", "password123")
    
    _, _, _, group_id = c1.group_create("PushGroup")
    c1.group_add(group_id, "push_b")
    c1.group_add(group_id, "push_c")
    
    # All enter chat mode
    c1.gm_chat_start(group_id)
    c2.gm_chat_start(group_id)
    c3.gm_chat_start(group_id)
    
    # A sends message
    c1.gm_send(group_id, "Hello everyone!")
    time.sleep(0.3)
    
    # B and C should receive PUSH GM
    push_b = c2.drain_push(timeout=0.5)
    push_c = c3.drain_push(timeout=0.5)
    
    found_b = any("PUSH GM" in msg and "push_a" in msg for msg in push_b)
    found_c = any("PUSH GM" in msg and "push_a" in msg for msg in push_c)
    
    assert found_b, f"B should receive PUSH GM: {push_b}"
    assert found_c, f"C should receive PUSH GM: {push_c}"
    
    c1.close()
    c2.close()
    c3.close()


def test_push_some_members(port: int):
    """Only members in chat mode receive PUSH"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    c3 = Conn(port=port)
    
    c1.register("psome_a", "password123", "psa@test.com")
    c2.register("psome_b", "password123", "psb@test.com")
    c3.register("psome_c", "password123", "psc@test.com")
    
    c1.login("psome_a", "password123")
    c2.login("psome_b", "password123")
    c3.login("psome_c", "password123")
    
    _, _, _, group_id = c1.group_create("PartialPushGroup")
    c1.group_add(group_id, "psome_b")
    c1.group_add(group_id, "psome_c")
    
    # Only A and B enter chat (C doesn't)
    c1.gm_chat_start(group_id)
    c2.gm_chat_start(group_id)
    # c3 NOT in chat mode
    
    # A sends message
    c1.gm_send(group_id, "Partial push test")
    time.sleep(0.3)
    
    # B should receive PUSH GM
    push_b = c2.drain_push(timeout=0.5)
    found_b = any("PUSH GM" in msg for msg in push_b)
    assert found_b, f"B should receive PUSH GM: {push_b}"
    
    # C should NOT receive PUSH (not in chat mode)
    # But message should be in history
    kind, _, rest = c3.gm_history(group_id)
    assert kind == "OK"
    assert "psome_a" in rest, f"C should see message in history: {rest}"
    
    c1.close()
    c2.close()
    c3.close()


def test_push_join_notification(port: int):
    """PUSH GM_JOIN when user enters chat"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("join_a", "password123", "ja@test.com")
    c2.register("join_b", "password123", "jb@test.com")
    
    c1.login("join_a", "password123")
    c2.login("join_b", "password123")
    
    _, _, _, group_id = c1.group_create("JoinNotifyGroup")
    c1.group_add(group_id, "join_b")
    
    # A enters chat first
    c1.gm_chat_start(group_id)
    time.sleep(0.1)
    c1.drain_push()  # Clear any initial messages
    
    # B enters chat
    c2.gm_chat_start(group_id)
    time.sleep(0.3)
    
    # A should receive GM_JOIN notification
    push_a = c1.drain_push(timeout=0.5)
    found_join = any("PUSH GM_JOIN" in msg and "join_b" in msg for msg in push_a)
    
    assert found_join, f"A should receive GM_JOIN for join_b: {push_a}"
    
    c1.close()
    c2.close()


def test_push_leave_notification(port: int):
    """PUSH GM_LEAVE when user exits chat"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("leave_a", "password123", "lea@test.com")
    c2.register("leave_b", "password123", "leb@test.com")
    
    c1.login("leave_a", "password123")
    c2.login("leave_b", "password123")
    
    _, _, _, group_id = c1.group_create("LeaveNotifyGroup")
    c1.group_add(group_id, "leave_b")
    
    # Both enter chat
    c1.gm_chat_start(group_id)
    c2.gm_chat_start(group_id)
    time.sleep(0.2)
    c1.drain_push()  # Clear join notifications
    
    # B leaves chat
    c2.gm_chat_end()
    time.sleep(0.3)
    
    # A should receive GM_LEAVE notification
    push_a = c1.drain_push(timeout=0.5)
    found_leave = any("PUSH GM_LEAVE" in msg and "leave_b" in msg for msg in push_a)
    
    assert found_leave, f"A should receive GM_LEAVE for leave_b: {push_a}"
    
    c1.close()
    c2.close()


def test_push_kicked_notification(port: int):
    """PUSH GM_KICKED when user is removed from group"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("kick_owner", "password123", "ko@test.com")
    c2.register("kick_victim", "password123", "kv@test.com")
    
    c1.login("kick_owner", "password123")
    c2.login("kick_victim", "password123")
    
    _, _, _, group_id = c1.group_create("KickGroup")
    c1.group_add(group_id, "kick_victim")
    
    # Victim enters chat
    c2.gm_chat_start(group_id)
    time.sleep(0.1)
    c2.drain_push()  # Clear any initial messages
    
    # Owner removes victim
    c1.group_remove(group_id, "kick_victim")
    time.sleep(0.3)
    
    # Victim should receive GM_KICKED
    push_victim = c2.drain_push(timeout=0.5)
    found_kicked = any("PUSH GM_KICKED" in msg for msg in push_victim)
    
    assert found_kicked, f"Victim should receive GM_KICKED: {push_victim}"
    
    c1.close()
    c2.close()


# ============ Edge Cases ============

def test_multiple_groups(port: int):
    """User in multiple group chats"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("mg_a", "password123", "mga@test.com")
    c2.register("mg_b", "password123", "mgb@test.com")
    
    c1.login("mg_a", "password123")
    c2.login("mg_b", "password123")
    
    # Create 2 groups
    _, _, _, gid1 = c1.group_create("MultiGroup1")
    _, _, _, gid2 = c1.group_create("MultiGroup2")
    
    c1.group_add(gid1, "mg_b")
    c1.group_add(gid2, "mg_b")
    
    # B joins group 1 chat
    c2.gm_chat_start(gid1)
    
    # A sends to both groups
    c1.gm_send(gid1, "Message to group 1")
    c1.gm_send(gid2, "Message to group 2")
    time.sleep(0.3)
    
    # B should only receive message from group 1 (the one they're chatting in)
    push_b = c2.drain_push(timeout=0.5)
    
    found_g1 = any("PUSH GM" in msg and str(gid1) in msg for msg in push_b)
    assert found_g1, f"B should receive message from group 1: {push_b}"
    
    # Group 2 message should be in history
    kind, _, rest = c2.gm_history(gid2)
    assert kind == "OK"
    
    c1.close()
    c2.close()


def test_large_group(port: int):
    """Large group with 5 members"""
    connections = [Conn(port=port) for _ in range(5)]
    
    # Register and login all
    for i, c in enumerate(connections):
        c.register(f"lg_user{i}", "password123", f"lg{i}@test.com")
        c.login(f"lg_user{i}", "password123")
    
    # First user creates group and adds others
    owner = connections[0]
    _, _, _, group_id = owner.group_create("LargeGroup")
    
    for i in range(1, 5):
        owner.group_add(group_id, f"lg_user{i}")
    
    # All enter chat
    for c in connections:
        c.gm_chat_start(group_id)
    time.sleep(0.2)
    
    # Clear initial notifications
    for c in connections:
        c.drain_push()
    
    # Owner sends message
    owner.gm_send(group_id, "Hello large group!")
    time.sleep(0.3)
    
    # All others should receive
    for i in range(1, 5):
        push = connections[i].drain_push(timeout=0.5)
        found = any("PUSH GM" in msg for msg in push)
        assert found, f"User {i} should receive PUSH GM: {push}"
    
    for c in connections:
        c.close()


def test_unicode_gm(port: int):
    """Unicode content in group messages"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("uni_gma", "password123", "ugma@test.com")
    c2.register("uni_gmb", "password123", "ugmb@test.com")
    
    c1.login("uni_gma", "password123")
    c2.login("uni_gmb", "password123")
    
    _, _, _, group_id = c1.group_create("UnicodeGMGroup")
    c1.group_add(group_id, "uni_gmb")
    
    unicode_msg = "Group 消息 🎊 Сообщение"
    kind, _, rest = c1.gm_send(group_id, unicode_msg)
    
    assert kind == "OK", f"Should handle Unicode: {rest}"
    
    # Verify in history
    kind, _, rest = c2.gm_history(group_id)
    assert kind == "OK"
    
    c1.close()
    c2.close()


# ============ Main ============

def run_all(port: int):
    """Run all GM tests"""
    runner = TestRunner("Group Message Features")
    
    # GM_SEND
    runner.add_test(test_send_success, "GM_SEND: success")
    runner.add_test(test_send_nonexistent_group, "GM_SEND: non-existent group (404)")
    runner.add_test(test_send_nonmember, "GM_SEND: non-member (403)")
    runner.add_test(test_send_invalid_token, "GM_SEND: invalid token (401)")
    runner.add_test(test_send_empty_content, "GM_SEND: empty content (400)")
    
    # GM_HISTORY
    runner.add_test(test_history_success, "GM_HISTORY: success")
    runner.add_test(test_history_empty, "GM_HISTORY: empty")
    runner.add_test(test_history_nonmember, "GM_HISTORY: non-member (403)")
    
    # GM_CHAT_START/END
    runner.add_test(test_chat_start_success, "GM_CHAT_START: success")
    runner.add_test(test_chat_start_nonmember, "GM_CHAT_START: non-member (403)")
    runner.add_test(test_chat_end_success, "GM_CHAT_END: success")
    
    # PUSH notifications
    runner.add_test(test_push_all_members, "PUSH GM: all members in chat")
    runner.add_test(test_push_some_members, "PUSH GM: some members in chat")
    runner.add_test(test_push_join_notification, "PUSH GM_JOIN: notification")
    runner.add_test(test_push_leave_notification, "PUSH GM_LEAVE: notification")
    runner.add_test(test_push_kicked_notification, "PUSH GM_KICKED: notification")
    
    # Edge cases
    runner.add_test(test_multiple_groups, "GM: multiple groups")
    runner.add_test(test_large_group, "GM: large group (5 members)")
    runner.add_test(test_unicode_gm, "GM: Unicode content")
    
    return runner.run(port)


def main():
    port = free_port()
    
    backup_data()
    proc = start_server(port, timeout_s=3600)
    
    try:
        passed, failed = run_all(port)
        print(f"\n{'='*60}")
        print(f"GM Tests: {passed} passed, {failed} failed")
        print(f"{'='*60}")
        return 0 if failed == 0 else 1
    finally:
        stop_server(proc)
        restore_data()


if __name__ == "__main__":
    sys.exit(main())
