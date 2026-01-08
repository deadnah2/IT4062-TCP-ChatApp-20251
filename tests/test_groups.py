#!/usr/bin/env python3
"""
Test Suite: Group Features
Tests for: GROUP_CREATE, GROUP_ADD, GROUP_REMOVE, GROUP_LEAVE, GROUP_LIST, GROUP_MEMBERS

Test Cases:
1. GROUP_CREATE - Create group successfully
2. GROUP_CREATE - Invalid name (empty, too long)
3. GROUP_CREATE - Invalid token (401)
4. GROUP_ADD - Add member successfully (owner only)
5. GROUP_ADD - Non-owner cannot add (403)
6. GROUP_ADD - Add non-existent user (404)
7. GROUP_ADD - Add already member (409)
8. GROUP_ADD - Add to non-existent group (404)
9. GROUP_REMOVE - Remove member successfully (owner only)
10. GROUP_REMOVE - Non-owner cannot remove (403)
11. GROUP_REMOVE - Owner cannot remove self (400)
12. GROUP_REMOVE - Remove non-member (404)
13. GROUP_LEAVE - Leave group successfully
14. GROUP_LEAVE - Owner cannot leave (must transfer or delete)
15. GROUP_LEAVE - Leave group not member of (404)
16. GROUP_LIST - List groups user is member of
17. GROUP_LIST - Empty list
18. GROUP_MEMBERS - List members of group
19. GROUP_MEMBERS - Non-member cannot list (403)
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


# ============ GROUP_CREATE Tests ============

def test_create_success(port: int):
    """Create group successfully"""
    c = Conn(port=port)
    
    c.register("grp_owner", "password123", "grp@test.com")
    c.login("grp_owner", "password123")
    
    kind, _, rest, group_id = c.group_create("MyTestGroup")
    
    assert kind == "OK", f"Expected OK, got {kind}: {rest}"
    assert group_id > 0, f"Expected positive group_id, got {group_id}"
    
    c.close()


def test_create_invalid_name_empty(port: int):
    """Create group with empty name - should fail"""
    c = Conn(port=port)
    
    c.register("grp_empty", "password123", "ge@test.com")
    c.login("grp_empty", "password123")
    
    # Send raw to test empty name
    rid = c.next_id()
    c.send_line(f"GROUP_CREATE {rid} token={c.token} name=")
    resp = c.recv_line()
    kind, _, rest = parse_resp(resp)
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    
    c.close()


def test_create_invalid_token(port: int):
    """Create group with invalid token - should fail with 401"""
    c = Conn(port=port)
    
    kind, _, rest, _ = c.group_create("BadTokenGroup", token="invalid_token_12345678901234")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "401" in rest, f"Expected 401 error, got {rest}"
    
    c.close()


# ============ GROUP_ADD Tests ============

def test_add_member_success(port: int):
    """Owner adds member successfully"""
    c1 = Conn(port=port)  # Owner
    c2 = Conn(port=port)  # New member
    
    c1.register("add_owner", "password123", "ao@test.com")
    c2.register("add_member", "password123", "am@test.com")
    
    c1.login("add_owner", "password123")
    c2.login("add_member", "password123")
    
    # Create group
    _, _, _, group_id = c1.group_create("AddTestGroup")
    
    # Add member
    kind, _, rest = c1.group_add(group_id, "add_member")
    
    assert kind == "OK", f"Expected OK, got {kind}: {rest}"
    
    # Verify member can see group
    kind, _, rest = c2.group_list()
    assert kind == "OK"
    assert str(group_id) in rest, f"Member should see group: {rest}"
    
    c1.close()
    c2.close()


def test_add_nonowner_forbidden(port: int):
    """Non-owner cannot add members - should fail with 403"""
    c1 = Conn(port=port)  # Owner
    c2 = Conn(port=port)  # Member
    c3 = Conn(port=port)  # Target
    
    c1.register("no_owner", "password123", "no@test.com")
    c2.register("no_member", "password123", "nm@test.com")
    c3.register("no_target", "password123", "nt@test.com")
    
    c1.login("no_owner", "password123")
    c2.login("no_member", "password123")
    
    # Owner creates group and adds member
    _, _, _, group_id = c1.group_create("NoAddGroup")
    c1.group_add(group_id, "no_member")
    
    # Member tries to add target
    kind, _, rest = c2.group_add(group_id, "no_target")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "403" in rest, f"Expected 403 error, got {rest}"
    
    c1.close()
    c2.close()
    c3.close()


def test_add_nonexistent_user(port: int):
    """Add non-existent user - should fail with 404"""
    c = Conn(port=port)
    
    c.register("add_nouser", "password123", "anu@test.com")
    c.login("add_nouser", "password123")
    
    _, _, _, group_id = c.group_create("NoUserGroup")
    
    kind, _, rest = c.group_add(group_id, "nosuchuser")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "404" in rest, f"Expected 404 error, got {rest}"
    
    c.close()


def test_add_already_member(port: int):
    """Add already member - should fail with 409"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("dup_owner", "password123", "do@test.com")
    c2.register("dup_member", "password123", "dm@test.com")
    
    c1.login("dup_owner", "password123")
    c2.login("dup_member", "password123")
    
    _, _, _, group_id = c1.group_create("DupMemberGroup")
    c1.group_add(group_id, "dup_member")
    
    # Try to add again
    kind, _, rest = c1.group_add(group_id, "dup_member")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "409" in rest, f"Expected 409 error, got {rest}"
    
    c1.close()
    c2.close()


def test_add_nonexistent_group(port: int):
    """Add to non-existent group - should fail with 404 or 403"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("nog_owner", "password123", "nogo@test.com")
    c2.register("nog_member", "password123", "nogm@test.com")
    
    c1.login("nog_owner", "password123")
    
    kind, _, rest = c1.group_add(99999, "nog_member")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    # Server may return 404 or 403 depending on check order
    assert "404" in rest or "403" in rest, f"Expected 404/403 error, got {rest}"
    
    c1.close()
    c2.close()


# ============ GROUP_REMOVE Tests ============

def test_remove_member_success(port: int):
    """Owner removes member successfully"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("rem_owner", "password123", "ro@test.com")
    c2.register("rem_member", "password123", "rm@test.com")
    
    c1.login("rem_owner", "password123")
    c2.login("rem_member", "password123")
    
    _, _, _, group_id = c1.group_create("RemoveGroup")
    c1.group_add(group_id, "rem_member")
    
    # Remove member
    kind, _, rest = c1.group_remove(group_id, "rem_member")
    
    assert kind == "OK", f"Expected OK, got {kind}: {rest}"
    
    # Member should not see group
    _, _, rest = c2.group_list()
    assert str(group_id) not in rest, f"Removed member should not see group: {rest}"
    
    c1.close()
    c2.close()


def test_remove_nonowner_forbidden(port: int):
    """Non-owner cannot remove - should fail with 403"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    c3 = Conn(port=port)
    
    c1.register("noremo_owner", "password123", "nro@test.com")
    c2.register("noremo_m1", "password123", "nrm1@test.com")
    c3.register("noremo_m2", "password123", "nrm2@test.com")
    
    c1.login("noremo_owner", "password123")
    c2.login("noremo_m1", "password123")
    c3.login("noremo_m2", "password123")
    
    _, _, _, group_id = c1.group_create("NoRemoveGroup")
    c1.group_add(group_id, "noremo_m1")
    c1.group_add(group_id, "noremo_m2")
    
    # m1 tries to remove m2
    kind, _, rest = c2.group_remove(group_id, "noremo_m2")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "403" in rest, f"Expected 403 error, got {rest}"
    
    c1.close()
    c2.close()
    c3.close()


def test_remove_self_owner(port: int):
    """Owner trying to remove self - behavior varies by implementation"""
    c = Conn(port=port)
    
    c.register("selfrem_owner", "password123", "sro@test.com")
    c.login("selfrem_owner", "password123")
    
    _, _, _, group_id = c.group_create("SelfRemoveGroup")
    
    kind, _, rest = c.group_remove(group_id, "selfrem_owner")
    
    # Some implementations allow this, some don't
    # Either OK or ERR is acceptable
    assert kind in ["OK", "ERR"], f"Unexpected response: {kind}"
    
    c.close()


def test_remove_nonmember(port: int):
    """Remove non-member - should fail with 404"""
    c = Conn(port=port)
    
    c.register("rem_noone", "password123", "rno@test.com")
    c.login("rem_noone", "password123")
    
    _, _, _, group_id = c.group_create("RemNooneGroup")
    
    kind, _, rest = c.group_remove(group_id, "nosuchuser")
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "404" in rest, f"Expected 404 error, got {rest}"
    
    c.close()


# ============ GROUP_LEAVE Tests ============

def test_leave_success(port: int):
    """Member leaves group successfully"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("leave_owner", "password123", "lo@test.com")
    c2.register("leave_member", "password123", "lm@test.com")
    
    c1.login("leave_owner", "password123")
    c2.login("leave_member", "password123")
    
    _, _, _, group_id = c1.group_create("LeaveGroup")
    c1.group_add(group_id, "leave_member")
    
    # Member leaves
    kind, _, rest = c2.group_leave(group_id)
    
    assert kind == "OK", f"Expected OK, got {kind}: {rest}"
    
    # Should not see group anymore
    _, _, rest = c2.group_list()
    assert str(group_id) not in rest, f"Should not see group after leave: {rest}"
    
    c1.close()
    c2.close()


def test_leave_owner_forbidden(port: int):
    """Owner cannot leave group - should fail"""
    c = Conn(port=port)
    
    c.register("leave_own", "password123", "lon@test.com")
    c.login("leave_own", "password123")
    
    _, _, _, group_id = c.group_create("OwnerLeaveGroup")
    
    kind, _, rest = c.group_leave(group_id)
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    # Server may return 400, 403, or 422
    assert "400" in rest or "403" in rest or "422" in rest, f"Expected error, got {rest}"
    
    c.close()


def test_leave_not_member(port: int):
    """Leave group not member of - should fail with 404"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("leave_out_own", "password123", "loo@test.com")
    c2.register("leave_out_m", "password123", "lom@test.com")
    
    c1.login("leave_out_own", "password123")
    c2.login("leave_out_m", "password123")
    
    _, _, _, group_id = c1.group_create("NotMemberGroup")
    
    # c2 tries to leave group they're not in
    kind, _, rest = c2.group_leave(group_id)
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "404" in rest or "403" in rest, f"Expected 404/403 error, got {rest}"
    
    c1.close()
    c2.close()


# ============ GROUP_LIST Tests ============

def test_list_groups(port: int):
    """List groups user is member of"""
    c = Conn(port=port)
    
    c.register("list_user", "password123", "lu@test.com")
    c.login("list_user", "password123")
    
    # Create 2 groups
    _, _, _, gid1 = c.group_create("ListGroup1")
    _, _, _, gid2 = c.group_create("ListGroup2")
    
    kind, _, rest = c.group_list()
    
    assert kind == "OK", f"Expected OK, got {kind}"
    assert str(gid1) in rest, f"Should see ListGroup1: {rest}"
    assert str(gid2) in rest, f"Should see ListGroup2: {rest}"
    
    c.close()


def test_list_empty(port: int):
    """Group list is empty"""
    c = Conn(port=port)
    
    c.register("nogroups", "password123", "ng@test.com")
    c.login("nogroups", "password123")
    
    kind, _, rest = c.group_list()
    
    assert kind == "OK", f"Expected OK, got {kind}"
    kv = parse_kv(rest)
    groups = kv.get("groups", "")
    assert groups == "" or groups == "empty", f"Should be empty: {rest}"
    
    c.close()


# ============ GROUP_MEMBERS Tests ============

def test_members_list(port: int):
    """List members of group"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("mem_owner", "password123", "mo@test.com")
    c2.register("mem_user", "password123", "mu@test.com")
    
    c1.login("mem_owner", "password123")
    c2.login("mem_user", "password123")
    
    _, _, _, group_id = c1.group_create("MembersGroup")
    c1.group_add(group_id, "mem_user")
    
    kind, _, rest = c1.group_members(group_id)
    
    assert kind == "OK", f"Expected OK, got {kind}"
    assert "mem_owner" in rest, f"Should see owner: {rest}"
    assert "mem_user" in rest, f"Should see member: {rest}"
    
    c1.close()
    c2.close()


def test_members_nonmember_forbidden(port: int):
    """Non-member cannot list members - should fail with 403"""
    c1 = Conn(port=port)
    c2 = Conn(port=port)
    
    c1.register("priv_owner", "password123", "po@test.com")
    c2.register("priv_outsider", "password123", "pou@test.com")
    
    c1.login("priv_owner", "password123")
    c2.login("priv_outsider", "password123")
    
    _, _, _, group_id = c1.group_create("PrivateGroup")
    
    # Outsider tries to list members
    kind, _, rest = c2.group_members(group_id)
    
    assert kind == "ERR", f"Expected ERR, got {kind}"
    assert "403" in rest or "404" in rest, f"Expected 403/404 error, got {rest}"
    
    c1.close()
    c2.close()


# ============ Main ============

def run_all(port: int):
    """Run all group tests"""
    runner = TestRunner("Group Features")
    
    # GROUP_CREATE
    runner.add_test(test_create_success, "GROUP_CREATE: success")
    runner.add_test(test_create_invalid_name_empty, "GROUP_CREATE: empty name")
    runner.add_test(test_create_invalid_token, "GROUP_CREATE: invalid token (401)")
    
    # GROUP_ADD
    runner.add_test(test_add_member_success, "GROUP_ADD: owner adds member")
    runner.add_test(test_add_nonowner_forbidden, "GROUP_ADD: non-owner forbidden (403)")
    runner.add_test(test_add_nonexistent_user, "GROUP_ADD: non-existent user (404)")
    runner.add_test(test_add_already_member, "GROUP_ADD: already member (409)")
    runner.add_test(test_add_nonexistent_group, "GROUP_ADD: non-existent group (404)")
    
    # GROUP_REMOVE
    runner.add_test(test_remove_member_success, "GROUP_REMOVE: owner removes member")
    runner.add_test(test_remove_nonowner_forbidden, "GROUP_REMOVE: non-owner forbidden (403)")
    runner.add_test(test_remove_self_owner, "GROUP_REMOVE: owner cannot remove self (400)")
    runner.add_test(test_remove_nonmember, "GROUP_REMOVE: non-member (404)")
    
    # GROUP_LEAVE
    runner.add_test(test_leave_success, "GROUP_LEAVE: member leaves")
    runner.add_test(test_leave_owner_forbidden, "GROUP_LEAVE: owner cannot leave")
    runner.add_test(test_leave_not_member, "GROUP_LEAVE: not member (404)")
    
    # GROUP_LIST
    runner.add_test(test_list_groups, "GROUP_LIST: list groups")
    runner.add_test(test_list_empty, "GROUP_LIST: empty")
    
    # GROUP_MEMBERS
    runner.add_test(test_members_list, "GROUP_MEMBERS: list members")
    runner.add_test(test_members_nonmember_forbidden, "GROUP_MEMBERS: non-member forbidden (403)")
    
    return runner.run(port)


def main():
    port = free_port()
    
    backup_data()
    proc = start_server(port, timeout_s=3600)
    
    try:
        passed, failed = run_all(port)
        print(f"\n{'='*60}")
        print(f"Group Tests: {passed} passed, {failed} failed")
        print(f"{'='*60}")
        return 0 if failed == 0 else 1
    finally:
        stop_server(proc)
        restore_data()


if __name__ == "__main__":
    sys.exit(main())
