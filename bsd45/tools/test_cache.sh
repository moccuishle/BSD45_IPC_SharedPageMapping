#!/usr/bin/env bash
# test_cache.sh - S17 acceptance: buffer cache + name cache + page cache.
#
# cat /readme.txt, snapshot /dev/vmstat, cat again, snapshot again: the second
# pass must come from memory (no new disk reads, no new page fills).
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

echo "S17 acceptance: buffer / name / page caches"
log="$("$ws/tools/run_qemu_test.sh" -Iso "$iso" \
    -Keys "shell|cat /readme.txt|vmstat|cat /readme.txt|vmstat|exit 0" \
    -WaitFor "user exit" -TimeoutSec 120)"

check "cache counters" "$log" "buf: total 64" "name: total 64" \
                             "page: total 128" "pages: alloc"
check "file read"      "$log" "BSD45-ELF-x86"
check "exit"           "$log" "[user exit 0]"

# last two samples of one counter line
samples() {   # samples <regex> -> two lines on stdout
    printf '%s\n' "$log" | grep -o "$1" | tail -2
}

buf_lines=$(samples 'buf: total [0-9]* used [0-9]* hits [0-9]* misses [0-9]* reads [0-9]* readahead [0-9]* dirty [0-9]*')
b1=$(printf '%s\n' "$buf_lines" | sed -n 1p)
b2=$(printf '%s\n' "$buf_lines" | sed -n 2p)
b1h=$(printf '%s' "$b1" | sed -n 's/.*hits \([0-9]*\).*/\1/p')
b2h=$(printf '%s' "$b2" | sed -n 's/.*hits \([0-9]*\).*/\1/p')
b1r=$(printf '%s' "$b1" | sed -n 's/.*reads \([0-9]*\).*/\1/p')
b2r=$(printf '%s' "$b2" | sed -n 's/.*reads \([0-9]*\).*/\1/p')
if [ -n "${b1h:-}" ] && [ -n "${b2h:-}" ]; then
    echo "  info buffer hits $b1h -> $b2h, disk reads $b1r -> $b2r"
    [ "$b2h" -gt "$b1h" ] && echo "  ok   buffer cache served the second pass" \
        || { echo "  FAIL buffer hits did not grow" >&2; fails=$((fails+1)); }
    [ "$b2r" -eq "$b1r" ] && echo "  ok   no extra disk reads on the second pass" \
        || { echo "  FAIL disk reads grew $b1r -> $b2r" >&2; fails=$((fails+1)); }
else
    echo "  FAIL could not parse buffer counters" >&2; fails=$((fails+1))
fi

p_lines=$(samples 'page: total [0-9]* used [0-9]* hits [0-9]* misses [0-9]* fills [0-9]* evicts [0-9]*')
p1=$(printf '%s\n' "$p_lines" | sed -n 1p)
p2=$(printf '%s\n' "$p_lines" | sed -n 2p)
p1h=$(printf '%s' "$p1" | sed -n 's/.*hits \([0-9]*\).*/\1/p')
p2h=$(printf '%s' "$p2" | sed -n 's/.*hits \([0-9]*\).*/\1/p')
p1f=$(printf '%s' "$p1" | sed -n 's/.*fills \([0-9]*\).*/\1/p')
p2f=$(printf '%s' "$p2" | sed -n 's/.*fills \([0-9]*\).*/\1/p')
if [ -n "${p1h:-}" ] && [ -n "${p2h:-}" ]; then
    echo "  info page hits $p1h -> $p2h, fills $p1f -> $p2f"
    [ "$p2h" -gt "$p1h" ] && echo "  ok   page cache served the second pass" \
        || { echo "  FAIL page hits did not grow" >&2; fails=$((fails+1)); }
    [ "$p2f" -eq "$p1f" ] && echo "  ok   no new page fills on the second pass" \
        || { echo "  FAIL page fills grew $p1f -> $p2f" >&2; fails=$((fails+1)); }
else
    echo "  FAIL could not parse page-cache counters" >&2; fails=$((fails+1))
fi

nh=$(printf '%s\n' "$log" | sed -n 's/.*name: total [0-9]* used [0-9]* hits \([0-9]*\).*/\1/p' | tail -1)
echo "  info name-cache hits ${nh:-?}"
if [ -n "${nh:-}" ] && [ "$nh" -gt 0 ]; then
    echo "  ok   name cache hits"
else
    echo "  FAIL name cache never hit" >&2; fails=$((fails+1))
fi

printf '%s' "$log" | grep -q 'TRAP' && { echo "  FAIL kernel trap" >&2; fails=$((fails+1)); }

if [ "$fails" -ne 0 ]; then echo "S17 caches: FAILED ($fails check(s))" >&2; exit 1; fi
echo "S17 caches: PASSED"
exit 0
