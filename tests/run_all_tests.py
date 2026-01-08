#!/usr/bin/env python3
"""
Master Test Runner for ChatProject-IT4062

Runs all test suites and provides summary:
- Base features (18 tests): Framing, Accounts, Sessions
- Friend features (18 tests): Invite, Accept, Reject, List, Delete
- Group features (19 tests): Create, Add, Remove, Leave, List, Members
- Private Message features (18 tests): Send, History, Conversations, Real-time
- Group Message features (19 tests): Send, History, Real-time, Notifications

Total: 92 test cases

Usage:
    python3 run_all_tests.py          # Run all tests
    python3 run_all_tests.py base     # Run only base tests
    python3 run_all_tests.py friends  # Run only friend tests
    python3 run_all_tests.py groups   # Run only group tests
    python3 run_all_tests.py pm       # Run only PM tests
    python3 run_all_tests.py gm       # Run only GM tests
"""

import sys
import os
import time

sys.path.insert(0, os.path.dirname(__file__))
from test_utils import (
    free_port, start_server, stop_server,
    backup_data, restore_data, section, Colors
)

# Import test modules
import test_base
import test_friends
import test_groups
import test_pm
import test_gm


def print_banner():
    """Print test banner"""
    print(f"""
{Colors.BOLD}╔══════════════════════════════════════════════════════════════╗
║          ChatProject-IT4062 - Integration Test Suite          ║
║                    Comprehensive Test Runner                   ║
╚══════════════════════════════════════════════════════════════╝{Colors.RESET}
""")


def print_summary(results: dict):
    """Print test summary"""
    total_passed = sum(r[0] for r in results.values())
    total_failed = sum(r[1] for r in results.values())
    total = total_passed + total_failed
    
    print(f"""
{Colors.BOLD}╔══════════════════════════════════════════════════════════════╗
║                        TEST SUMMARY                           ║
╠══════════════════════════════════════════════════════════════╣{Colors.RESET}""")
    
    for suite, (passed, failed) in results.items():
        total_suite = passed + failed
        status = f"{Colors.GREEN}✓{Colors.RESET}" if failed == 0 else f"{Colors.RED}✗{Colors.RESET}"
        print(f"║  {status} {suite:<20} {passed:>3}/{total_suite:<3} passed")
    
    print(f"""{Colors.BOLD}╠══════════════════════════════════════════════════════════════╣{Colors.RESET}""")
    
    if total_failed == 0:
        print(f"║  {Colors.GREEN}✓ ALL TESTS PASSED: {total_passed}/{total}{Colors.RESET}")
    else:
        print(f"║  {Colors.RED}✗ TESTS FAILED: {total_failed}/{total}{Colors.RESET}")
    
    print(f"""{Colors.BOLD}╚══════════════════════════════════════════════════════════════╝{Colors.RESET}
""")


def run_all():
    """Run all test suites"""
    print_banner()
    
    port = free_port()
    results = {}
    
    backup_data()
    proc = start_server(port, timeout_s=3600)
    
    try:
        # Run each test suite
        suites = [
            ("Base", test_base.run_all),
            ("Friends", test_friends.run_all),
            ("Groups", test_groups.run_all),
            ("Private Message", test_pm.run_all),
            ("Group Message", test_gm.run_all),
        ]
        
        for name, run_fn in suites:
            try:
                passed, failed = run_fn(port)
                results[name] = (passed, failed)
            except Exception as e:
                print(f"{Colors.RED}[ERROR] {name} suite crashed: {e}{Colors.RESET}")
                results[name] = (0, 1)
        
        print_summary(results)
        
        # Return exit code
        total_failed = sum(r[1] for r in results.values())
        return 0 if total_failed == 0 else 1
        
    finally:
        stop_server(proc)
        restore_data()


def run_single(suite_name: str):
    """Run a single test suite"""
    suites = {
        "base": ("Base", test_base.run_all),
        "friends": ("Friends", test_friends.run_all),
        "groups": ("Groups", test_groups.run_all),
        "pm": ("Private Message", test_pm.run_all),
        "gm": ("Group Message", test_gm.run_all),
    }
    
    if suite_name.lower() not in suites:
        print(f"Unknown suite: {suite_name}")
        print(f"Available: {', '.join(suites.keys())}")
        return 1
    
    name, run_fn = suites[suite_name.lower()]
    
    print(f"\n{Colors.BOLD}Running {name} Tests...{Colors.RESET}\n")
    
    port = free_port()
    backup_data()
    proc = start_server(port, timeout_s=3600)
    
    try:
        passed, failed = run_fn(port)
        
        print(f"\n{'='*60}")
        if failed == 0:
            print(f"{Colors.GREEN}✓ {name}: {passed}/{passed + failed} tests passed{Colors.RESET}")
        else:
            print(f"{Colors.RED}✗ {name}: {failed} tests failed{Colors.RESET}")
        print(f"{'='*60}\n")
        
        return 0 if failed == 0 else 1
        
    finally:
        stop_server(proc)
        restore_data()


def main():
    if len(sys.argv) > 1:
        return run_single(sys.argv[1])
    else:
        return run_all()


if __name__ == "__main__":
    sys.exit(main())
