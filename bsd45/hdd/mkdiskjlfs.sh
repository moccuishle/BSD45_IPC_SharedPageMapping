#!/usr/bin/env bash
# mkdiskjlfs.sh -- build an "already installed" BSD45 virtual disk on the host.
#
# Pure Bash port of mkdiskjlfs.py.  Binary data is written with dd; python is
# NEVER invoked.
#
# Usage:
#   bash mkdiskjlfs.sh -o build/testdisk.raw \
#        [--size N] [--jlfs F] [--kernel F] [--stage2 F] [--mbr F]
#
# Defaults (identical to mkdiskjlfs.py, relative to this script's directory):
#   --jlfs    hdd/BSD45.JLFS
#   --kernel build/bsd45.elf
#   --stage2 build/stage2.bin
#   --mbr    build/stage1_hdd.embed.bin
#   --size   67108864
#
# Disk layout:
#   LBA 0    MBR boot sector (stage1_hdd.embed.bin)
#   LBA 1    stage2.bin
#   LBA 8192 raw kernel ELF
#   LBA 0..  JLFS image (its first 8 KiB is the boot block)
#   The MBR's "BDMF" manifest is patched with kernel_lba/len, stage2_len, 0.

set -u

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
WS=$(dirname "$HERE")

SECSZ=512
KERNEL_LBA=8192
STAGE2_SECS=8
DISK_SIZE=$((64 * 1024 * 1024))

OUT="$WS/build/testdisk.raw"
SIZE=$DISK_SIZE
JLFS="$HERE/BSD45.JLFS"
KERNEL="$WS/build/bsd45.elf"
STAGE2="$WS/build/stage2.bin"
MBR="$WS/build/stage1_hdd.embed.bin"

while (($#)); do
    case "$1" in
        -o|--out) OUT="$2"; shift 2 ;;
        --size) SIZE="$2"; shift 2 ;;
        --jlfs) JLFS="$2"; shift 2 ;;
        --kernel) KERNEL="$2"; shift 2 ;;
        --stage2) STAGE2="$2"; shift 2 ;;
        --mbr) MBR="$2"; shift 2 ;;
        -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
        -*) printf 'unknown option: %s\n' "$1" >&2; exit 2 ;;
        *) printf 'unexpected argument: %s\n' "$1" >&2; exit 2 ;;
    esac
done

for p in "$JLFS" "$KERNEL" "$STAGE2" "$MBR"; do
    if [[ ! -f "$p" ]]; then
        printf 'missing input: %s\n' "$p" >&2
        exit 2
    fi
done

mbr_size=$(stat -c %s "$MBR")
if ((mbr_size != SECSZ)); then
    printf 'MBR must be 512 bytes, got %d\n' "$mbr_size" >&2
    exit 2
fi
stage2_size=$(stat -c %s "$STAGE2")
if ((stage2_size > STAGE2_SECS * SECSZ)); then
    printf 'stage2 too big: %d\n' "$stage2_size" >&2
    exit 2
fi
jlfs_size=$(stat -c %s "$JLFS")
if ((jlfs_size > KERNEL_LBA * SECSZ)); then
    printf 'JLFS image overlaps the kernel LBA\n' >&2
    exit 2
fi
kernel_size=$(stat -c %s "$KERNEL")

# little-endian hex helper
rev_hex_bytes() {
    local h="$1" out="" i
    for ((i = ${#h} - 2; i >= 0; i -= 2)); do out+="${h:i:2}"; done
    printf '%s' "$out"
}
le_hex() {
    local v="$1" n="$2" h
    h=$(printf "%0$((n * 2))x" "$v")
    rev_hex_bytes "$h"
}

# Locate "BDMF" inside the MBR.
idx=$(LC_ALL=C grep -aob 'BDMF' "$MBR" | head -1 | cut -d: -f1)
if [[ -z "$idx" ]]; then
    printf 'MBR has no BDMF manifest\n' >&2
    exit 2
fi

mkdir -p "$(dirname "$OUT")"
# Start from a guaranteed all-zero image (Python's bytearray(size)).
: > "$OUT"
truncate -s "$SIZE" "$OUT"

# Same write order as mkdiskjlfs.py.
dd if="$JLFS" of="$OUT" bs="$SECSZ" conv=notrunc status=none
dd if="$MBR" of="$OUT" bs="$SECSZ" conv=notrunc status=none
dd if="$STAGE2" of="$OUT" bs="$SECSZ" seek=1 conv=notrunc status=none
dd if="$KERNEL" of="$OUT" bs="$SECSZ" seek="$KERNEL_LBA" conv=notrunc status=none

# struct.pack_into('<IIII', img, idx+4, KERNEL_LBA, len(kernel), len(stage2), 0)
patch_hex="$(le_hex "$KERNEL_LBA" 4)$(le_hex "$kernel_size" 4)$(le_hex "$stage2_size" 4)00000000"
printf '%s' "$patch_hex" | xxd -r -p |
    dd of="$OUT" bs=1 seek=$((idx + 4)) conv=notrunc status=none

printf 'disk: %s (%d bytes)\n' "$OUT" "$SIZE"
printf '  jlfs    %d bytes @LBA 0\n' "$jlfs_size"
printf '  mbr    512 bytes @LBA 0\n'
printf '  stage2 %d bytes @LBA 1\n' "$stage2_size"
printf '  kernel %d bytes @LBA %d\n' "$kernel_size" "$KERNEL_LBA"
exit 0
