#!/usr/bin/env bash
# test_badptr.sh - os.md M1: access_ok/copyin/copyout return EFAULT.
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

echo "os.md M1 acceptance: access_ok / EFAULT"
log="$("$ws/tools/run_qemu_test.sh" -Iso "$iso" \
    -Keys "shell|/bin/badptr|exit 0" -WaitFor "user exit" -TimeoutSec 90)"

check "unmapped"  "$log" "badptr: unmapped write -> -14"
check "read-only" "$log" "badptr: read into text -> -14"
check "verdict"   "$log" "badptr: PASS"

printf '%s' "$log" | grep -q 'kernel TRAP' && { echo "  FAIL kernel trap" >&2; fails=$((fails+1)); }

if [ "$fails" -ne 0 ]; then echo "os.md M1 badptr: FAILED ($fails)" >&2; exit 1; fi
echo "os.md M1 badptr: PASSED"
exit 0
