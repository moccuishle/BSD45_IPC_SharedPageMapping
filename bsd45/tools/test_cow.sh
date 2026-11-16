#!/usr/bin/env bash
# test_cow.sh - os.md M2/M3: copy-on-write fork + fault copy.
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

echo "os.md M2/M3 acceptance: COW fork + page-fault copy"
log="$("$ws/tools/run_qemu_test.sh" -Iso "$iso" \
    -Keys "shell|/bin/cowtest|exit 0" -WaitFor "cowtest: PASS" -TimeoutSec 90)"

check "child"   "$log" "cowtest: child x=99"
check "parent"  "$log" "cowtest: parent x=42"
check "counter" "$log" "cow: shared"
check "verdict" "$log" "cowtest: PASS"

line="$(printf '%s' "$log" | grep -oE 'cow: shared [0-9]+ faults [0-9]+ copies [0-9]+' | head -1 || true)"
if [ -n "$line" ]; then
    echo "  info $line"
    f=$(echo "$line" | awk '{print $5}'); c=$(echo "$line" | awk '{print $7}')
    if [ "$f" -gt 0 ] && [ "$c" -gt 0 ]; then echo "  ok   COW path exercised"
    else echo "  FAIL COW counters not exercised" >&2; fails=$((fails+1)); fi
else
    echo "  FAIL could not parse COW counters" >&2; fails=$((fails+1))
fi

printf '%s' "$log" | grep -q 'kernel TRAP' && { echo "  FAIL kernel trap" >&2; fails=$((fails+1)); }

if [ "$fails" -ne 0 ]; then echo "os.md M2/M3 COW: FAILED ($fails)" >&2; exit 1; fi
echo "os.md M2/M3 COW: PASSED"
exit 0
