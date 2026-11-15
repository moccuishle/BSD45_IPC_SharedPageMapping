#!/usr/bin/env bash
# test_mmap.sh - S17 acceptance: file/anonymous/shared mmap and msync.
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

echo "S17 acceptance: mmap + page cache"
log="$("$ws/tools/run_qemu_test.sh" -Iso "$iso" \
    -Keys "shell|/bin/mmaptest|exit 0" -WaitFor "user exit" -TimeoutSec 90)"

check "private file map" "$log" "mmap: file ok size=1066 match=1"
check "anonymous map"    "$log" "mmap: anon ok"
check "shared + msync"   "$log" "mmap: shared ok"
check "overall"          "$log" "mmap: ok"
check "exit"             "$log" "[user exit 0]"

printf '%s' "$log" | grep -q 'TRAP' && { echo "  FAIL kernel trap" >&2; fails=$((fails+1)); }

if [ "$fails" -ne 0 ]; then echo "S17 mmap: FAILED ($fails check(s))" >&2; exit 1; fi
echo "S17 mmap: PASSED"
exit 0
