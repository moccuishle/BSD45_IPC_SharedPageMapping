#!/usr/bin/env bash
# test_ipl.sh - os.md mechanism 4: 16-level IPL, unified pending, order.
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

echo "os.md M4 acceptance: IPL 0..15 / unified pending / inheritance"
log="$("$ws/tools/run_qemu_test.sh" -Iso "$iso" \
    -Keys "ipl|ipltest|ipltrace|ipl" -WaitFor "ipltest: PASS" -TimeoutSec 60)"

check "levels"        "$log" "ipl: 16 levels" "clock: PIT 100 Hz" "IRQ0 -> IPL 5"
check "vector map"    "$log" "ipltest: vector map ok"
check "raise/restore" "$log" "ipltest: raise/restore ok"
check "strict order"  "$log" "ipltest: order 12>7>3 ok"
check "inheritance"   "$log" "ipltest: thread ipl/inheritance ok"
check "verdict"       "$log" "ipltest: PASS"
check "trace"         "$log" "itrace:"

printf '%s' "$log" | grep -q 'kernel TRAP' && { echo "  FAIL kernel trap" >&2; fails=$((fails+1)); }

if [ "$fails" -ne 0 ]; then echo "os.md M4 IPL: FAILED ($fails check(s))" >&2; exit 1; fi
echo "os.md M4 IPL: PASSED"
exit 0
