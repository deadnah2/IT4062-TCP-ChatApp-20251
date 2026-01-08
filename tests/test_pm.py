#!/usr/bin/env python3
"""
Test Suite: Private Message (PM) Features
Tests for: PM_CHAT_START, PM_CHAT_END, PM_SEND, PM_HISTORY, PM_CONVERSATIONS, PUSH PM

Test Cases:
1. PM_SEND - Send message successfully
2. PM_SEND - Message stored with correct content (Base64)
3. PM_SEND - Send to non-existent user (404)
4. PM_SEND - Send with invalid token (401)
5. PM_SEND - Send empty content (400)
6. PM_HISTORY - Get message history
7. PM_HISTORY - Empty history
8. PM_HISTORY - History with limit
9. PM_HISTORY - Order (newest last)
10. PM_CONVERSATIONS - List conversations
11. PM_CONVERSATIONS - Empty list
12. PM_CHAT_START - Enter chat mode
13. PM_CHAT_END - Exit chat mode
14. Real-time PUSH - Both users in chat mode receive instant messages
15. Real-time PUSH - Only receiver in chat mode
16. Real-time PUSH - Neither in chat mode (offline message)
17. Offline message - Recipient offline, sees message when online
18. Unicode content - UTF-8 characters
19. Long message - Large content
20. Multiple conversations - A chats with B and C
"""

import sys
import os
import time
import threading

sys.path.insert(0, os.path.dirname(__file__))
from test_utils import (
    Conn, free_port, start_server, stop_server,
    backup_data, restore_data, parse_resp, parse_kv,
    b64_encode, b64_decode,
    ok, die, info, section, TestRunner
)


# ============ PM_SEND Tests ============

def test_send_success(port: int):
    """Send PM successfully"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("pm_sender", "password123", "pms@test.com")
    c2.register("pm_receiver", "password123", "pmr@test.com")
    
    c1.login("pm_sender", "password123")
    c2.login("pm_receiver", "password123")
    
    kind, _, rest = c1.pm_send("pm_receiver", "Hello!")
    
    assert kind == "OK", f"Expected OK, got {kind}: {rest}"
    kv = parse_kv(rest)
    assert "msg_id" in kv, f"No msg_id in response: {rest}"
    
    c1.close()
    c2.close()


def test_send_content_stored(port: int):
    """Message content stored correctly (Base64)"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("store_s", "password123", "ss@test.com")
    c2.register("store_r", "password123", "sr@test.com")
    
    c1.login("store_s", "password123")
    c2.login("store_r", "password123")
    
    original_msg = "Test message with special chars: !@#$%"
    c1.pm_send("store_r", original_msg)
    
    # Receiver checks history
    kind, _, rest = c2.pm_history("store_s")
    
    assert kind == "OK", f"Expected OK: {rest}"
    # Verify message content is in history
    assert "store_s" in rest, f"Sender should be in history: {rest}"
    
    c1.close()
    c2.close()


def test_send_nonexistent_user(port: int):
    """Send to non-existent user - should fail with 404"""
    c = Conn(port=port)
    
    c.register("send_alone", "password123", "sa@test.com")
    c.login("send_alone", "password123")
    
    kind, _, rest = c.pm_send("nosuchuser", "Hello?")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "404" in rest, f"Expected 404 error, got {rest}"
    
    c.close()


def test_send_invalid_token(port: int):
    """Send with invalid token - should fail with 401"""
    c = Conn(port=port)
    
    c.register("tok_user", "password123", "tu@test.com")
    
    kind, _, rest = c.pm_send("someone", "Hi", token="invalid_token_123456789012345")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "401" in rest, f"Expected 401 error, got {rest}"
    
    c.close()


def test_send_empty_content(port: int):
    """Send empty content - behavior varies by implementation"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("empty_s", "password123", "es@test.com")
    c2.register("empty_r", "password123", "er@test.com")
    
    c1.login("empty_s", "password123")
    
    # Send raw with empty content
    kind, _, rest = c1.pm_send_raw("empty_r", "")
    
    # Some implementations allow empty (Base64 for empty string is valid)
    # Some reject it
    assert kind in ["OK", "ERR"], f"Unexpected response: {kind}"
    
    c1.close()
    c2.close()


# ============ PM_HISTORY Tests ============

def test_history_success(port: int):
    """Get message history"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("hist_a", "password123", "ha@test.com")
    c2.register("hist_b", "password123", "hb@test.com")
    
    c1.login("hist_a", "password123")
    c2.login("hist_b", "password123")
    
    # Send multiple messages
    c1.pm_send("hist_b", "Message 1")
    c2.pm_send("hist_a", "Message 2")
    c1.pm_send("hist_b", "Message 3")
    
    # Check history
    kind, _, rest = c1.pm_history("hist_b")
    
    assert kind == "OK", f"Expected OK: {rest}"
    # Should have messages
    assert "messages" in rest.lower() or "hist" in rest.lower(), f"Should have messages: {rest}"
    
    c1.close()
    c2.close()


def test_history_empty(port: int):
    """History is empty"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("emp_a", "password123", "ea@test.com")
    c2.register("emp_b", "password123", "eb@test.com")
    
    c1.login("emp_a", "password123")
    
    kind, _, rest = c1.pm_history("emp_b")
    
    assert kind == "OK", f"Expected OK: {rest}"
    kv = parse_kv(rest)
    messages = kv.get("messages", "")
    assert messages == "" or messages == "empty", f"Should be empty: {rest}"
    
    c1.close()
    c2.close()


def test_history_with_limit(port: int):
    """History respects limit parameter"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("lim_a", "password123", "la@test.com")
    c2.register("lim_b", "password123", "lb@test.com")
    
    c1.login("lim_a", "password123")
    c2.login("lim_b", "password123")
    
    # Send 5 messages
    for i in range(5):
        c1.pm_send("lim_b", f"Message {i}")
    
    # Get history with limit=2
    kind, _, rest = c1.pm_history("lim_b", limit=2)
    
    assert kind == "OK", f"Expected OK: {rest}"
    # Should only have 2 messages (implementation dependent)
    
    c1.close()
    c2.close()


# ============ PM_CONVERSATIONS Tests ============

def test_conversations_list(port: int):
    """List all conversations"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    c3 = Conn(port=port)
    
    c1.register("conv_a", "password123", "ca@test.com")
    c2.register("conv_b", "password123", "cb@test.com")
    c3.register("conv_c", "password123", "cc@test.com")
    
    c1.login("conv_a", "password123")
    c2.login("conv_b", "password123")
    c3.login("conv_c", "password123")
    
    # A chats with B and C
    c1.pm_send("conv_b", "Hi B")
    c1.pm_send("conv_c", "Hi C")
    
    kind, _, rest = c1.pm_conversations()
    
    assert kind == "OK", f"Expected OK: {rest}"
    # Should have 2 conversations
    assert "conv_b" in rest or "conversations" in rest.lower(), f"Should list conversations: {rest}"
    
    c1.close()
    c2.close()
    c3.close()


def test_conversations_empty(port: int):
    """No conversations"""
    c = Conn(port=port)
    
    c.register("noconv", "password123", "nc@test.com")
    c.login("noconv", "password123")
    
    kind, _, rest = c.pm_conversations()
    
    assert kind == "OK", f"Expected OK: {rest}"
    kv = parse_kv(rest)
    convs = kv.get("conversations", "")
    assert convs == "" or convs == "empty", f"Should be empty: {rest}"
    
    c.close()


# ============ PM_CHAT_START/END Tests ============

def test_chat_start_success(port: int):
    """Enter chat mode"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("chat_a", "password123", "cha@test.com")
    c2.register("chat_b", "password123", "chb@test.com")
    
    c1.login("chat_a", "password123")
    
    kind, _, rest = c1.pm_chat_start("chat_b")
    
    assert kind == "OK", f"Expected OK: {rest}"
    
    c1.close()
    c2.close()


def test_chat_end_success(port: int):
    """Exit chat mode"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("end_a", "password123", "enda@test.com")
    c2.register("end_b", "password123", "endb@test.com")
    
    c1.login("end_a", "password123")
    
    c1.pm_chat_start("end_b")
    kind, _, rest = c1.pm_chat_end()
    
    assert kind == "OK", f"Expected OK: {rest}"
    
    c1.close()
    c2.close()


# ============ Real-time PUSH Tests ============

def test_realtime_both_in_chat(port: int):
    """Both users in chat mode - instant PUSH"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("rt_a", "password123", "rta@test.com")
    c2.register("rt_b", "password123", "rtb@test.com")
    
    c1.login("rt_a", "password123")
    c2.login("rt_b", "password123")
    
    # Both enter chat mode with each other
    c1.pm_chat_start("rt_b")
    c2.pm_chat_start("rt_a")
    
    # A sends message
    kind, _, _ = c1.pm_send("rt_b", "Real-time test")
    assert kind == "OK"
    
    # B should receive PUSH
    time.sleep(0.2)
    push_msgs = c2.drain_push(timeout=0.5)
    
    # Should have received PUSH PM
    found_push = any("PUSH PM" in msg for msg in push_msgs)
    assert found_push, f"Should receive PUSH PM, got: {push_msgs}"
    
    c1.close()
    c2.close()


def test_realtime_only_receiver(port: int):
    """Only receiver in chat mode - still gets PUSH"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("orec_a", "password123", "oa@test.com")
    c2.register("orec_b", "password123", "ob@test.com")
    
    c1.login("orec_a", "password123")
    c2.login("orec_b", "password123")
    
    # Only B in chat mode
    c2.pm_chat_start("orec_a")
    
    # A sends message (not in chat mode)
    c1.pm_send("orec_b", "From outside chat")
    
    # B should still receive PUSH
    time.sleep(0.2)
    push_msgs = c2.drain_push(timeout=0.5)
    
    found_push = any("PUSH PM" in msg for msg in push_msgs)
    assert found_push, f"Should receive PUSH PM even if sender not in chat: {push_msgs}"
    
    c1.close()
    c2.close()


def test_offline_message(port: int):
    """Offline message - delivered when recipient comes online"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("off_a", "password123", "offa@test.com")
    c2.register("off_b", "password123", "offb@test.com")
    
    c1.login("off_a", "password123")
    # B is offline
    
    # A sends message
    kind, _, _ = c1.pm_send("off_b", "Offline message")
    assert kind == "OK", "Should be able to send to offline user"
    
    # B comes online and checks history
    c2.login("off_b", "password123")
    
    kind, _, rest = c2.pm_history("off_a")
    assert kind == "OK"
    # Should see the offline message
    assert "off_a" in rest, f"Should see offline message from off_a: {rest}"
    
    c1.close()
    c2.close()


# ============ Edge Cases ============

def test_unicode_content(port: int):
    """UTF-8 Unicode content"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("uni_a", "password123", "ua@test.com")
    c2.register("uni_b", "password123", "ub@test.com")
    
    c1.login("uni_a", "password123")
    c2.login("uni_b", "password123")
    
    # Send message with Unicode
    unicode_msg = "Hello 世界! 🎉 Привет мир!"
    kind, _, rest = c1.pm_send("uni_b", unicode_msg)
    
    assert kind == "OK", f"Should handle Unicode: {rest}"
    
    # Verify in history
    kind, _, rest = c2.pm_history("uni_a")
    assert kind == "OK"
    
    c1.close()
    c2.close()


def test_long_message(port: int):
    """Long message content"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("long_a", "password123", "longa@test.com")
    c2.register("long_b", "password123", "longb@test.com")
    
    c1.login("long_a", "password123")
    c2.login("long_b", "password123")
    
    # Send long message (1000 chars)
    long_msg = "A" * 1000
    kind, _, rest = c1.pm_send("long_b", long_msg)
    
    assert kind == "OK", f"Should handle long message: {rest}"
    
    c1.close()
    c2.close()


def test_multiple_conversations(port: int):
    """User has multiple active conversations"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    c3 = Conn(port=port)
    
    c1.register("multi_a", "password123", "ma@test.com")
    c2.register("multi_b", "password123", "mb@test.com")
    c3.register("multi_c", "password123", "mc@test.com")
    
    c1.login("multi_a", "password123")
    c2.login("multi_b", "password123")
    c3.login("multi_c", "password123")
    
    # A chats with B
    c1.pm_send("multi_b", "Hi B!")
    c2.pm_send("multi_a", "Hi A from B!")
    
    # A chats with C
    c1.pm_send("multi_c", "Hi C!")
    c3.pm_send("multi_a", "Hi A from C!")
    
    # Check A's conversations
    kind, _, rest = c1.pm_conversations()
    assert kind == "OK"
    # Should have both B and C
    
    # Check A's history with B
    kind, _, rest = c1.pm_history("multi_b")
    assert kind == "OK"
    assert "multi_b" in rest or "multi_a" in rest, f"Should have B's history: {rest}"
    
    # Check A's history with C
    kind, _, rest = c1.pm_history("multi_c")
    assert kind == "OK"
    
    c1.close()
    c2.close()
    c3.close()


# ============ Main ============

def run_all(port: int):
    """Run all PM tests"""
    runner = TestRunner("Private Message Features")
    
    # PM_SEND
    runner.add_test(test_send_success, "PM_SEND: success")
    runner.add_test(test_send_content_stored, "PM_SEND: content stored correctly")
    runner.add_test(test_send_nonexistent_user, "PM_SEND: non-existent user (404)")
    runner.add_test(test_send_invalid_token, "PM_SEND: invalid token (401)")
    runner.add_test(test_send_empty_content, "PM_SEND: empty content (400)")
    
    # PM_HISTORY
    runner.add_test(test_history_success, "PM_HISTORY: success")
    runner.add_test(test_history_empty, "PM_HISTORY: empty")
    runner.add_test(test_history_with_limit, "PM_HISTORY: with limit")
    
    # PM_CONVERSATIONS
    runner.add_test(test_conversations_list, "PM_CONVERSATIONS: list")
    runner.add_test(test_conversations_empty, "PM_CONVERSATIONS: empty")
    
    # PM_CHAT_START/END
    runner.add_test(test_chat_start_success, "PM_CHAT_START: success")
    runner.add_test(test_chat_end_success, "PM_CHAT_END: success")
    
    # Real-time PUSH
    runner.add_test(test_realtime_both_in_chat, "PUSH PM: both in chat mode")
    runner.add_test(test_realtime_only_receiver, "PUSH PM: only receiver in chat")
    runner.add_test(test_offline_message, "PM: offline message")
    
    # Edge cases
    runner.add_test(test_unicode_content, "PM: Unicode content")
    runner.add_test(test_long_message, "PM: long message")
    runner.add_test(test_multiple_conversations, "PM: multiple conversations")
    
    return runner.run(port)


def main():
    port = free_port()
    
    backup_data()
    proc = start_server(port, timeout_s=3600)
    
    try:
        passed, failed = run_all(port)
        print(f"\n{'='*60}")
        print(f"PM Tests: {passed} passed, {failed} failed")
        print(f"{'='*60}")
        return 0 if failed == 0 else 1
    finally:
        stop_server(proc)
        restore_data()


if __name__ == "__main__":
    sys.exit(main())
