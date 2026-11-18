#!/usr/bin/env bash
# make_install_jlfs.sh -- build a native-JLFS install image (3 MiB) for BSD45.
#
# JLFS counterpart of the original make_install_ffs.sh: same userland selection,
# then calls mkjlfs.sh (not mkffs.sh) with -size 3MiB.  The output has a native
# JLFS CG bitmap, so jlfs_mount() mounts it rw without a rebuild scan.  python
# is NEVER invoked.  The original make_install_ffs.sh / mkffs.sh are NOT modified.
#
# Usage: bash make_install_jlfs.sh [output.img]
# Default output: hdd/BSD45.JLFS (3145728 bytes).
#
# Optional environment override (for testing with a reference userland):
#   BSD45_USERLAND=DIR   use DIR instead of <workspace>/out/userland.

set -u

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
WS=$(dirname "$HERE")
USERLAND="${BSD45_USERLAND:-$WS/out/userland}"
OUT="${1:-$HERE/BSD45.JLFS}"

FILES=(
    "/sbin/init:init.elf"
    "/bin/sh:sh.elf"
    "/hello:hello.elf"
)
TOOLS=(echo ls cat mkdir rm cp mv wc grep head tail df mount ps uname sleep
       kill true false date vmstat test)

spec=$(mktemp "${TMPDIR:-/tmp}/bsd45_jlfs_XXXXXX.txt") || exit 2

for pair in "${FILES[@]}"; do
    dest="${pair%%:*}"
    name="${pair##*:}"
    src="$USERLAND/$name"
    if [[ -f "$src" ]]; then
        printf '%s=%s\n' "$dest" "$src" >> "$spec"
    else
        printf 'make_install_jlfs: WARNING missing %s\n' "$src" >&2
    fi
done
for name in "${TOOLS[@]}"; do
    src="$USERLAND/$name.elf"
    if [[ -f "$src" ]]; then
        printf '/bin/%s=%s\n' "$name" "$src" >> "$spec"
    fi
done

if ! grep -q '^/bin/sh=' "$spec" || ! grep -q '^/sbin/init=' "$spec"; then
    printf 'make_install_jlfs: init.elf or sh.elf missing in out/userland\n' >&2
    rm -f "$spec"
    exit 2
fi

bash "$HERE/mkjlfs.sh" -o "$OUT" -size $((3 * 1024 * 1024)) --spec-file "$spec"
rc=$?
rm -f "$spec"
exit $rc
