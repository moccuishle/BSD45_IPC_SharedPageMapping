#!/usr/bin/env python3
"""
mkffs.py - build a minimal but valid 4.4BSD-Lite2 FFS (UFS1) disk image.

It writes a standard superblock (struct fs), an inode table (struct dinode)
and POSIX directories (struct direct), then copies the caller's files in.
The on-disk format follows 4.4BSD-Lite2 usr/src/sys/ufs/ffs/fs.h,
ufs/dinode.h and ufs/dir.h exactly, so BSD45's ffs.c can mount it read-only.

Geometry: DEV_BSIZE=512, fs_bsize=4096, fs_fsize=512 (frag), one cylinder
group.  fsbtodb=0 because one fragment is exactly one 512-byte sector.

Usage:
    python3 mkffs.py -o disk.img -size 67108864 \
        /BSD45.ELF=kernel.elf /sbin/init=init.elf /bin/sh=sh.elf ...
"""

import argparse
import os
import re
import struct
import sys

DEV_BSIZE = 512
BSIZE = 4096
FSIZE = 512
FRAG = BSIZE // FSIZE          # 8
BSHIFT = 12
FSHIFT = 9
FRAGSHIFT = 3
DEV_BSHIFT = 9
FSBT_ODB = 0                   # frag(512B) == device sector(512B)

# file type bits (dinode.h)
IFMT = 0o170000
IFDIR = 0o040000
IFREG = 0o100000
IFLNK = 0o120000

MAXNAMLEN = 255
INOSIZE = 128
INOPB = BSIZE // INOSIZE       # 32 inodes per 4096 block
FS_MAGIC = 0x011954
MAXMNTLEN = 512
MAXCSBUFS = 31                 # (128/4)-1 on i386
NINDIR = BSIZE // 4            # 1024 pointers per indirect block

# struct direct.d_type values (ufs/dir.h)
DT_UNKNOWN = 0
DT_DIR = 4
DT_REG = 8


def csum_bytes(ndir, nbfree, nifree, nffree):
    return struct.pack('<iiii', ndir, nbfree, nifree, nffree)


def build_superblock(total_frags, ipg, ncg, iblkno, dblkno, csaddr,
                     csaddr_frags, fpg, ndir, nifree):
    """Serialize struct fs using EXPLICIT i386 byte offsets (no field-order
    guessing).  Offsets were derived from 4.4BSD-Lite2 sys/ufs/ffs/fs.h."""
    b = bytearray(8192)                      # SBSIZE

    def i32(off, val):
        b[off:off + 4] = struct.pack('<I', val & 0xffffffff)

    sblkno = 8192 // DEV_BSIZE                # 16
    cblkno = sblkno + 8192 // BSIZE           # 18

    i32(0, 0)             # fs_firstfield
    i32(4, 0)             # fs_unused_1
    i32(8, sblkno)        # fs_sblkno
    i32(12, cblkno)       # fs_cblkno
    i32(16, iblkno)       # fs_iblkno      <-- ffs.c reads 16
    i32(20, dblkno)       # fs_dblkno      <-- ffs.c reads 20
    i32(24, 0)            # fs_cgoffset
    i32(28, 0)            # fs_cgmask
    i32(32, 0)            # fs_time
    i32(36, total_frags)  # fs_size
    i32(40, total_frags)  # fs_dsize
    i32(44, ncg)          # fs_ncg
    i32(48, BSIZE)        # fs_bsize       <-- ffs.c reads 48
    i32(52, FSIZE)        # fs_fsize       <-- 52
    i32(56, FRAG)         # fs_frag        <-- 56
    i32(60, 0)            # fs_minfree
    i32(64, 0)            # fs_rotdelay
    i32(68, 0)            # fs_rps
    i32(72, (~(BSIZE - 1)) & 0xffffffff)   # fs_bmask
    i32(76, (~(FSIZE - 1)) & 0xffffffff)   # fs_fmask
    i32(80, BSHIFT)       # fs_bshift
    i32(84, FSHIFT)       # fs_fshift
    i32(88, 0)            # fs_maxcontig
    i32(92, 0)            # fs_maxbpg
    i32(96, FRAGSHIFT)    # fs_fragshift
    i32(100, FSBT_ODB)    # fs_fsbtodb     <-- ffs.c reads 100
    i32(104, 8192)        # fs_sbsize
    i32(108, 0)           # fs_csmask
    i32(112, 0)           # fs_csshift
    i32(116, NINDIR)      # fs_nindir      <-- ffs.c reads 116
    i32(120, INOPB)       # fs_inopb       <-- ffs.c reads 120
    i32(124, 1)           # fs_nspf
    i32(128, 0)           # fs_optim
    i32(132, 0)           # fs_npsect
    i32(136, 0)           # fs_interleave
    i32(140, 0)           # fs_trackskew
    i32(144, 0)           # fs_headswitch
    i32(148, 0)           # fs_trkseek
    i32(152, csaddr)      # fs_csaddr
    i32(156, csaddr_frags * DEV_BSIZE)  # fs_cssize
    i32(160, BSIZE)       # fs_cgsize
    i32(164, 0)           # fs_ntrak
    i32(168, 0)           # fs_nsect
    i32(172, 0)           # fs_spc
    i32(176, 0)           # fs_ncyl
    i32(180, 0)           # fs_cpg
    i32(184, ipg)         # fs_ipg         <-- ffs.c reads 184
    i32(188, fpg)         # fs_fpg
    b[192:208] = csum_bytes(ndir, total_frags, nifree, total_frags)  # fs_cstotal
    b[208] = 0            # fs_fmod
    b[209] = 1            # fs_clean
    b[210] = 0            # fs_ronly
    b[211] = 0            # fs_flags
    b[212:212 + 2] = b'/' # fs_fsmnt (remainder stays zero)
    i32(724, 0)           # fs_cgrotor
    # fs_csp[31] @728..851, fs_maxcluster @852 -> left zero (pointers, unused)
    i32(856, 0)           # fs_cpc
    # fs_opostbl[16][8] @860..1115, fs_sparecon[50] @1116..1315 -> zero
    i32(1316, 0)          # fs_contigsumsize
    i32(1320, 0)          # fs_maxsymlinklen
    i32(1324, 2)          # fs_inodefmt (FS_64BITINO used by 4.4BSD dinode)
    b[1328:1336] = struct.pack('<Q', (1 << 63) - 1)  # fs_maxfilesize
    b[1336:1344] = struct.pack('<q', ~(BSIZE - 1))   # fs_qbmask
    b[1344:1352] = struct.pack('<q', ~(FSIZE - 1))   # fs_qfmask
    i32(1352, 0)          # fs_state
    i32(1356, 0)          # fs_postblformat
    i32(1360, 0)          # fs_nrpos
    i32(1364, 0)          # fs_postbloff
    i32(1368, 0)          # fs_rotbloff
    i32(1372, FS_MAGIC)   # fs_magic       <-- ffs.c reads 1372

    # sanity: the offsets ffs.c depends on
    assert struct.unpack_from('<i', b, 16)[0] == iblkno
    assert struct.unpack_from('<i', b, 48)[0] == BSIZE
    assert struct.unpack_from('<i', b, 120)[0] == INOPB
    assert struct.unpack_from('<i', b, 1372)[0] == FS_MAGIC
    return bytes(b)


def build_dinode(mode, nlink, size, blocks, direct, indirect=None):
    """Serialize struct dinode (128 bytes)."""
    b = bytearray(INOSIZE)
    off = 0

    def put(fmt, *vals):
        nonlocal off
        data = struct.pack('<' + fmt, *vals)
        b[off:off + len(data)] = data
        off += len(data)

    put('Hh', mode & 0xffff, nlink)          # di_mode, di_nlink
    put('I', 0)                              # di_u.oldids[2]
    put('Q', size)                           # di_size (64-bit)
    put('iiiiii', 0, 0, 0, 0, 0, 0)          # atime/atimensec, mtime/mtimensec,
                                             # ctime/ctimensec
    db = list(direct) + [0] * (12 - len(direct))
    put('12i', *db[:12])                     # di_db[12]
    ib = list(indirect or []) + [0] * (3 - len(indirect or []))
    put('3i', *ib[:3])                       # di_ib[3]
    put('II', 0, blocks)                     # di_flags, di_blocks
    put('i', 1)                              # di_gen
    put('II', 0, 0)                          # di_uid, di_gid
    put('ii', 0, 0)                          # di_spare[2]
    assert off == INOSIZE, off
    return bytes(b)


def dirent(ino, dtype, name):
    """One struct direct + padded name (DIRSIZ rounds to 4)."""
    raw = name.encode('ascii')
    namlen = len(raw)
    reclen = (8 + namlen + 1 + 3) & ~3
    h = struct.pack('<IHBB', ino, reclen, dtype, namlen)
    return h + raw + b'\x00' * (reclen - len(h) - namlen)


# MSYS2 / Git Bash rewrites any argv that starts with '/' into a Windows
# path, e.g. "/sbin/init=init.elf" -> "C:/.../PortableGit/sbin/init=init.elf".
# Detect that form and recover the original absolute in-image path.  A
# caller can also pass --spec-file to avoid argv rewriting entirely.
_DRIVE_ABS = re.compile(r'^[A-Za-z]:[/\\]')
_KNOWN_DIRS = {'bin', 'sbin', 'etc', 'usr', 'home', 'tmp', 'sys', 'include',
               'lib', 'var', 'dev', 'proc'}


def normalize_dest(dest):
    """Undo MSYS/Git-Bash path conversion on a `/path=src` argument."""
    dest = dest.replace('\\', '/')
    if not _DRIVE_ABS.match(dest):
        return dest
    parts = [p for p in dest.split('/') if p and not re.match(r'^[A-Za-z]:$', p)]
    # Drop the drive-prefixed install root: keep from the last known BSD
    # top-level directory, otherwise keep only the final component.
    for i in range(len(parts) - 1, -1, -1):
        if parts[i] in _KNOWN_DIRS:
            return '/' + '/'.join(parts[i:])
    return '/' + (parts[-1] if parts else '')


def load_specs(args):
    """Build the (dest, src) list from positional args and/or --spec-file.

    --spec-file is a text file with one `dest=src` per line ('#' comments
    allowed).  Reading specs from a file is the reliable way to invoke
    mkffs.py under MSYS/Git Bash, which otherwise rewrites leading '/'.
    """
    specs = list(args.files)
    if args.spec_file:
        with open(args.spec_file, 'r', encoding='utf-8') as f:
            for raw in f:
                line = raw.strip()
                if not line or line.startswith('#'):
                    continue
                specs.append(line)
    return specs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-o', '--out', required=True)
    ap.add_argument('-size', '--size', type=int, default=64 * 1024 * 1024)
    ap.add_argument('-ipg', '--ipg', type=int, default=128)
    ap.add_argument('--spec-file', default=None,
                    help='file with one dest=src per line (MSYS-safe)')
    ap.add_argument('files', nargs='*', help='dest=src pairs, e.g. /bin/sh=sh.elf')
    args = ap.parse_args()

    total_frags = args.size // FSIZE
    ipg = args.ipg
    ncg = 1
    fpg = total_frags

    # fixed layout (in fragments)
    iblkno = 32                              # inode table starts at frag 32
    inode_frags = (ipg // INOPB) * FRAG       # 128/32=4 blocks *8 = 32 frags
    dblkno = iblkno + inode_frags             # data starts at frag 64
    csaddr = 24                              # cg summary at frag 24
    csaddr_frags = 1

    # parse file list into (dest_path, source_bytes)
    entries = []
    for spec in load_specs(args):
        if '=' not in spec:
            print(f'bad spec (need dest=src): {spec}', file=sys.stderr)
            return 2
        dest, src = spec.split('=', 1)
        dest = normalize_dest(dest)
        src = os.path.expandvars(src)
        with open(src, 'rb') as f:
            entries.append((dest, f.read()))

    image = bytearray(args.size)

    # ---- build the directory tree ----------------------------------------
    # Represent nodes: path -> {ino, type, data, children:set}
    nodes = {'/': {'ino': 2, 'type': IFDIR, 'children': []}}
    next_ino = 3
    file_data = []                           # (frag_no, bytes)
    indir_data = []                          # (frag_no, [block pointers])
    next_frag = dblkno

    def alloc_frag():
        nonlocal next_frag
        f = next_frag
        next_frag += FRAG
        return f

    def ensure_dir(path):
        parts = [p for p in path.split('/') if p]
        cur = ''
        for p in parts[:-1]:
            cur += '/' + p
            if cur not in nodes:
                nodes[cur] = {'ino': next_ino, 'type': IFDIR, 'children': []}
                nonlocal_vars = True
                ino = next_ino
                next_ino_local = ino + 1
                # update outer via closure workaround below
                nodes[cur]['_local'] = next_ino_local
                # we use a mutable holder instead
                holder[0] = next_ino_local
                next_ino = next_ino_local
        parent = cur or '/'
        return parent, parts[-1] if parts else ''

    holder = [next_ino]
    for dest, data in entries:
        if not dest.startswith('/'):
            dest = '/' + dest
        parts = [p for p in dest.split('/') if p]
        if not parts:
            continue
        # create parent dirs, using holder for next_ino
        parent = '/'
        for p in parts[:-1]:
            child = (parent.rstrip('/') + '/' + p)
            if child not in nodes:
                nodes[child] = {'ino': holder[0], 'type': IFDIR, 'children': []}
                holder[0] += 1
                nodes[parent]['children'].append((p, IFDIR, nodes[child]['ino']))
            parent = child
        name = parts[-1]
        ino = holder[0]
        holder[0] += 1
        # Allocate whole 4096-byte blocks.  First NDADDR via di_db[], the
        # rest through one single-indirect block in di_ib[0].
        nblocks = (len(data) + BSIZE - 1) // BSIZE
        if nblocks == 0:
            nblocks = 1                      # keep at least one data block
        direct = [alloc_frag() for _ in range(min(nblocks, 12))]
        indirect = []
        indir_frag = 0
        if nblocks > 12:
            indir_frag = alloc_frag()        # the indirect block itself
            extra = [alloc_frag() for _ in range(nblocks - 12)]
            indirect = extra
            indir_data.append((indir_frag, extra))
        for i, blk in enumerate(direct):
            chunk = data[i * BSIZE:(i + 1) * BSIZE]
            file_data.append((blk * DEV_BSIZE, chunk))
        for i, blk in enumerate(indirect):
            chunk = data[(12 + i) * BSIZE:(13 + i) * BSIZE]
            file_data.append((blk * DEV_BSIZE, chunk))
        nodes[dest] = {'ino': ino, 'type': IFREG, 'size': len(data),
                       'direct': direct, 'indirect': indirect,
                       'indir_frag': indir_frag, 'nblocks': nblocks}
        nodes[parent]['children'].append((name, IFREG, ino))

    next_ino = holder[0]

    # root '..' points to itself; subdir '..' points to parent
    for path, node in nodes.items():
        if node['type'] != IFDIR:
            continue
        if path != '/':
            parent = path.rsplit('/', 1)[0] or '/'
            node['children'].insert(0, ('..', IFDIR, nodes[parent]['ino']))
        node['children'].insert(0, ('.', IFDIR, node['ino']))

    # ---- write directory data blocks -------------------------------------
    for path, node in nodes.items():
        if node['type'] != IFDIR:
            continue
        blob = bytearray()
        for name, dtype, ino in node['children']:
            blob += dirent(ino, DT_DIR if dtype == IFDIR else DT_REG, name)
        if len(blob) > BSIZE:
            print(f'directory too big: {path}', file=sys.stderr)
            return 2
        frag = alloc_frag()
        image[frag * DEV_BSIZE:frag * DEV_BSIZE + len(blob)] = blob
        node['frag'] = frag
        node['size'] = len(blob)

    # ---- write indirect blocks (single-indirect pointer arrays) ----------
    for indir_frag, ptrs in indir_data:
        arr = bytearray(BSIZE)
        for i, blk in enumerate(ptrs[:NINDIR]):
            struct.pack_into('<i', arr, i * 4, blk)
        image[indir_frag * DEV_BSIZE:indir_frag * DEV_BSIZE + BSIZE] = arr

    # ---- write file data --------------------------------------------------
    for off, data in file_data:
        image[off:off + len(data)] = data

    # ---- write inode table ------------------------------------------------
    def mode_for(node):
        if node['type'] == IFDIR:
            return IFDIR | 0o755
        return IFREG | 0o644

    for path, node in nodes.items():
        ino = node['ino']
        if ino <= 0 or ino > ipg:
            continue
        off = iblkno * DEV_BSIZE + (ino - 1) * INOSIZE
        if node['type'] == IFDIR:
            direct = [node['frag']]
            indirect = []
            nblocks = FRAG if node['size'] else 0
        else:
            direct = node['direct']
            indirect = node['indirect']
            nblocks = node['nblocks'] * FRAG
        # di_ib[0] is the fragment number of the single-indirect block
        indir_ptrs = [node.get('indir_frag', 0)] if node.get('indir_frag', 0) else []
        din = build_dinode(mode_for(node),
                           2 if node['type'] == IFDIR else 1,
                           node['size'], nblocks, direct, indir_ptrs)
        image[off:off + INOSIZE] = din

    # ---- superblock -------------------------------------------------------
    ndir = sum(1 for n in nodes.values() if n['type'] == IFDIR)
    nifree = ipg - len(nodes)
    sb = build_superblock(total_frags, ipg, ncg, iblkno, dblkno, csaddr,
                          csaddr_frags, fpg, ndir, nifree)
    image[8192:8192 + len(sb)] = sb

    # cg summary (minimal, enough for a read-only mount)
    cg = bytearray(BSIZE)
    struct.pack_into('<i', cg, 0x0, next_frag)       # cg_nbbkup approx
    image[csaddr * DEV_BSIZE:csaddr * DEV_BSIZE + BSIZE] = cg

    if len(image) != args.size:
        print(f'image overflow: used {len(image)} > size {args.size}; '
              f'increase -size', file=sys.stderr)
        return 2
    if next_frag > total_frags:
        print(f'out of fragments: used {next_frag} > {total_frags}; '
              f'increase -size', file=sys.stderr)
        return 2

    with open(args.out, 'wb') as f:
        f.write(image)

    print(f'FFS image: {args.out} ({len(image)} bytes)')
    print(f'  superblock @8192  magic=0x{FS_MAGIC:06x}  bsize={BSIZE} fsize={FSIZE}')
    print(f'  inodes: {len(nodes)}/{ipg}  inode_table@frag{iblkno}  data@frag{dblkno}')
    for path, node in sorted(nodes.items()):
        print(f'  ino {node["ino"]:3d}  {node["type"]:06o}  {path}'
              + (f'  {node["size"]}B' if node['type'] == IFREG else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
