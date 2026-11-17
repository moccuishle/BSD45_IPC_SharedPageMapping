#!/usr/bin/env bash
# make_install_ffs.sh -- build the FFS image written to the VMware disk by
# the in-guest `install` command (placed on the ISO as BSD45.FFS).
#
# Pure Bash port of make_install_ffs.py.  Thin wrapper over mkffs.sh: selects
# the userland ELFs, writes a spec file, then calls mkffs.sh with -size 3MiB.
# python/python3 is NEVER invoked.
#
# Usage:
#   bash make_install_ffs.sh [output.img]
# Default output: hdd/BSD45.FFS (3145728 bytes).
#
# Optional environment override (for testing with a reference userland):
#   BSD45_USERLAND=DIR   use DIR instead of <workspace>/out/userland.
# With no override the behaviour is identical to make_install_ffs.py.

set -u

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
WS=$(dirname "$HERE")
USERLAND="${BSD45_USERLAND:-$WS/out/userland}"
OUT="${1:-$HERE/BSD45.FFS}"

# dest-on-disk -> userland ELF basename
FILES=(
    "/sbin/init:init.elf"
    "/bin/sh:sh.elf"
    "/hello:hello.elf"
)

# optional core tools (included only when built)
TOOLS=(echo ls cat mkdir rm cp mv wc grep head tail df mount ps uname sleep
       kill true false date vmstat test)

spec=$(mktemp "${TMPDIR:-/tmp}/bsd45_ffs_XXXXXX.txt") || exit 2

for pair in "${FILES[@]}"; do
    dest="${pair%%:*}"
    name="${pair##*:}"
    src="$USERLAND/$name"
    if [[ -f "$src" ]]; then
        printf '%s=%s\n' "$dest" "$src" >> "$spec"
    else
        printf 'make_install_ffs: WARNING missing %s\n' "$src" >&2
    fi
done
for name in "${TOOLS[@]}"; do
    src="$USERLAND/$name.elf"
    if [[ -f "$src" ]]; then
        printf '/bin/%s=%s\n' "$name" "$src" >> "$spec"
    fi
done

if ! grep -q '^/bin/sh=' "$spec" || ! grep -q '^/sbin/init=' "$spec"; then
    printf 'make_install_ffs: init.elf or sh.elf missing in out/userland\n' >&2
    rm -f "$spec"
    exit 2
fi

bash "$HERE/mkffs.sh" -o "$OUT" -size $((3 * 1024 * 1024)) --spec-file "$spec"
rc=$?
rm -f "$spec"
exit $rc
