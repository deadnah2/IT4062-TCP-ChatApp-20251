#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
make clean
make
python3 tests/itest.py
