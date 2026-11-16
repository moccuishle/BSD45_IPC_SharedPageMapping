#!/usr/bin/env bash
# test_fault.sh - os.md M1: a user fault kills only the faulting process.
set -uo pipefail
ws="$(cd "$(dirname "$0")/.." && pwd)"
iso="${1:-$ws/out/BSD45-ELF-x86.iso}"
[ -f "$iso" ] || { echo "ISO not found: $iso" >&2; exit 1; }
fails=0

check() {
    local name="$1" log="$2" n
    shift 2
    for n in "$@"; do
        if printf '%s' "$log" | grep -qF -- "$n"; then echo "  ok   $name: found $n"
        else echo "  FAIL $name: missing $n" >&2; fails=$((fails + 1)); fi
    done
}

echo "os.md M1 acceptance: user fault isolation"
log="$("$ws/tools/run_qemu_test.sh" -Iso "$iso" \
    -Keys "shell|/bin/crash|echo alive|exit 0" -WaitFor "user exit" -TimeoutSec 90)"

check "fault"    "$log" "crash: about to fault"
check "signal"   "$log" "user fault: pid" "sig 11"
check "survived" "$log" "alive"
check "exit"     "$log" "[user exit 0]"

printf '%s' "$log" | grep -q 'kernel TRAP' && { echo "  FAIL kernel trap" >&2; fails=$((fails+1)); }

if [ "$fails" -ne 0 ]; then echo "os.md M1 fault: FAILED ($fails)" >&2; exit 1; fi
echo "os.md M1 fault: PASSED"
exit 0
