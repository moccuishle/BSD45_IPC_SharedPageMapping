#!/usr/bin/env bash
#
# Copyright (c) 2026 jialiang.  All rights reserved.
#

# test_wpath.sh - S29 acceptance: kstr path through the VFS.
set -uo pipefail
ws="$(cd "$(dirname "$0")/.." && pwd)"
iso="${1:-$ws/out/BSD45-ELF-x86.iso}"
[ -f "$iso" ] || { echo "ISO not found: $iso" >&2; exit 1; }
log="$("$ws/tools/run_qemu_test.sh" -Iso "$iso" -Keys "wtest" \
    -WaitFor "wtest: PASS" -TimeoutSec 60)"
fails=0
for n in "wtest: kstr path" "wtest: PASS"; do
    printf '%s' "$log" | grep -qF -- "$n" && echo "  ok   found $n" ||
        { echo "  FAIL missing $n" >&2; fails=$((fails+1)); }
done
printf '%s' "$log" | grep -q 'TRAP' && fails=$((fails+1))
[ "$fails" -eq 0 ] || { echo "S29 wpath: FAILED" >&2; exit 1; }
echo "S29 wpath: PASSED"
exit 0
