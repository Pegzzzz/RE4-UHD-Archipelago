#!/bin/sh
# Usage: tools/gen_test.sh [extra Generate.py args]  (expects Archipelago checkout at $AP, default /root/Archipelago)
AP=${AP:-/root/AP067}
cd "$AP" && SKIP_REQUIREMENTS_UPDATE=1 timeout 600 python3 Generate.py --skip_output "$@" < /dev/null 2>&1 | grep -v "^\s*$" | tail -12
