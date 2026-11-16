#!/usr/bin/env bash
#
# Copyright (c) 2026 jialiang.  All rights reserved.
#

# test_pthread.sh - S26 acceptance: user pthread_create/join/detach.
set -uo pipefail
ws="$(cd "$(dirname "$0")/.." && pwd)"
iso="${1:-$ws/out/BSD45-ELF-x86.iso}"
[ -f "$iso" ] || { echo "ISO not found: $iso" >&2; exit 1; }
fails=0

check() {
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

echo "S26 acceptance: pthread_create / pthread_join / pthread_detach"
log="$("$ws/tools/run_qemu_test.sh" -Iso "$iso" \
    -Keys "shell|/bin/pthread|exit 0" -WaitFor "user exit" -TimeoutSec 120)"

check "create"  "$log" "pthreadtest: created 4 threads"
check "join"    "$log" "pthreadtest: counter=4000 join=ok"
check "detach"  "$log" "pthreadtest: detach counter=5000"
check "verdict" "$log" "pthreadtest: PASS"

printf '%s' "$log" | grep -q 'TRAP' && { echo "  FAIL kernel trap" >&2; fails=$((fails+1)); }

if [ "$fails" -ne 0 ]; then echo "S26 pthread: FAILED ($fails check(s))" >&2; exit 1; fi
echo "S26 pthread: PASSED"
exit 0
