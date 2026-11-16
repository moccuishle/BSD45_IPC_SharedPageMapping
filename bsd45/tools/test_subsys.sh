#!/usr/bin/env bash
#
# Copyright (c) 2026 jialiang.  All rights reserved.
#

# test_subsys.sh - S20 acceptance: API personalities decoupled from kernel.
set -uo pipefail
ws="$(cd "$(dirname "$0")/.." && pwd)"
iso="${1:-$ws/out/BSD45-ELF-x86.iso}"
[ -f "$iso" ] || { echo "ISO not found: $iso" >&2; exit 1; }
fails=0

check() {   # check <name> <log> <needle...>
    local name="$1" log="$2" n
    shift 2
    for n in "$@"; do
        if printf '%s' "$log" | grep -qF -- "$n"; then
            echo "  ok   $name: found $n"
        else
            echo "  FAIL $name: missing $n" >&2
            fails=$((fails + 1))
        fi
    done
}

echo "S20 acceptance: API subsystems (unix/posix/linux/win32)"
log="$("$ws/tools/run_qemu_test.sh" -Iso "$iso" \
    -Keys "subsystest|subsys|shell|/bin/subtest|exit 0" \
    -WaitFor "user exit" -TimeoutSec 90)"

check "kernel registry" "$log" "subsystest: 4 personalities ok"
check "kernel verdict"  "$log" "subsystest: PASS"
check "active default"  "$log" "subsys: 4 personalities, active unix"
check "unix ABI"        "$log" "subtest: unix pid=1"
check "posix ABI"       "$log" "subtest: posix pid=1"
check "linux ABI"       "$log" "subtest: linux pid=1" "linux-write-ok"
check "win32 ABI"       "$log" "subtest: win32 pid=1"
check "user verdict"    "$log" "subtest: PASS"
check "exit"            "$log" "[user exit 0]"

printf '%s' "$log" | grep -q 'TRAP' && { echo "  FAIL kernel trap" >&2; fails=$((fails+1)); }

if [ "$fails" -ne 0 ]; then echo "S20 subsystems: FAILED ($fails check(s))" >&2; exit 1; fi
echo "S20 subsystems: PASSED"
exit 0
