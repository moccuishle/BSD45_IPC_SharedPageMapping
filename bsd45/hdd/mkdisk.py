#!/usr/bin/env python3
"""mkdisk.py - build an 'already installed' BSD45 virtual disk on the host.

Same byte layout that the guest `install` command produces, so the boot
path can be tested without running the ISO install every time:

    LBA 0           512-byte MBR boot sector (boot/i386/stage1_hdd.s)
    LBA 1..8        stage2 ELF loader (build/stage2.bin, 4096 bytes)
    LBA 8192        raw kernel ELF (build/bsd45.elf)
    LBA 0..6144     the FFS image (its first 8 KiB is the boot block, so
                    the MBR/stage2 writes above are harmless)

Usage: mkdisk.py -o build/testdisk.raw
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
WS = os.path.dirname(HERE)

SECSZ = 512
KERNEL_LBA = 8192
STAGE2_SECS = 8
DISK_SIZE = 64 * 1024 * 1024


def read(path):
    with open(path, 'rb') as f:
        return f.read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-o', '--out', default=os.path.join(WS, 'build', 'testdisk.raw'))
    ap.add_argument('--size', type=int, default=DISK_SIZE)
    ap.add_argument('--ffs', default=os.path.join(HERE, 'BSD45.FFS'))
    ap.add_argument('--kernel', default=os.path.join(WS, 'build', 'bsd45.elf'))
    ap.add_argument('--stage2', default=os.path.join(WS, 'build', 'stage2.bin'))
    ap.add_argument('--mbr', default=os.path.join(WS, 'build', 'stage1_hdd.embed.bin'))
    args = ap.parse_args()

    for p in (args.ffs, args.kernel, args.stage2, args.mbr):
        if not os.path.exists(p):
            print(f'missing input: {p}', file=sys.stderr)
            return 2

    ffs = read(args.ffs)
    kernel = read(args.kernel)
    stage2 = read(args.stage2)
    mbr = read(args.mbr)

    if len(mbr) != SECSZ:
        print(f'MBR must be 512 bytes, got {len(mbr)}', file=sys.stderr)
        return 2
    if len(stage2) > STAGE2_SECS * SECSZ:
        print(f'stage2 too big: {len(stage2)}', file=sys.stderr)
        return 2
    if len(ffs) > KERNEL_LBA * SECSZ:
        print('FFS image overlaps the kernel LBA', file=sys.stderr)
        return 2

    img = bytearray(args.size)
    img[0:len(ffs)] = ffs
    img[0:SECSZ] = mbr
    img[SECSZ:SECSZ + len(stage2)] = stage2
    koff = KERNEL_LBA * SECSZ
    img[koff:koff + len(kernel)] = kernel

    # patch the manifest inside the MBR so stage1 knows where the kernel is
    idx = mbr.find(b'BDMF')
    if idx < 0:
        print('MBR has no BDMF manifest', file=sys.stderr)
        return 2
    struct.pack_into('<IIII', img, idx + 4, KERNEL_LBA, len(kernel),
                     len(stage2), 0)

    with open(args.out, 'wb') as f:
        f.write(img)
    print(f'disk: {args.out} ({len(img)} bytes)')
    print(f'  ffs    {len(ffs)} bytes @LBA 0')
    print(f'  mbr    512 bytes @LBA 0')
    print(f'  stage2 {len(stage2)} bytes @LBA 1')
    print(f'  kernel {len(kernel)} bytes @LBA {KERNEL_LBA}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
