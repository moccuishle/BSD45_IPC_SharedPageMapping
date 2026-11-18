#!/usr/bin/env python3
"""pe_bss_fix.py - turn all-zero .data$xxx COFF sections into .bss$xxx.

Why this exists
---------------
When the kernel is compiled with MSYS2's i686-w64-mingw32-gcc (a PE/COFF
target) using -fdata-sections, every *static* zero-initialised object is
emitted into a section named ".data$<var>" with real contents (a block of
zeros) instead of a bss section.  A static 256 KB array therefore adds
256 KB to the object file and, after linking, to the .data section of the
kernel image - the bootloader then has to read that many bytes off the CD
and fails on the resulting oversized image.

The same source compiled with a Linux ELF toolchain puts those objects in
.bss (SHT_NOBITS), which occupies no file space at all - that is why the
Ubuntu-built kernel is ~123 KB while the Windows-built one is ~572 KB.

This tool rewrites PE/COFF object files in place so that every section that
  * is named .data$<something>,
  * has a non-empty raw payload,
  * carries no relocations, and
  * whose payload is entirely zero
is renamed to .bss$<something>, gets SizeOfRawData = 0 and is marked
IMAGE_SCN_CNT_UNINITIALIZED_DATA.  That is a pure representation change:
a section full of zeros with no relocations is by definition uninitialised
data, so the linked kernel behaves exactly the same, it is just not stored
in the file any more.

Sections that contain real initialised data, or that carry relocations, are
left untouched, so this can never corrupt a real initialiser.

Usage:  python pe_bss_fix.py <file.o> [more.o ...]
        python pe_bss_fix.py --recurse <dir>
"""

import os
import struct
import sys

IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040
IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080


def section_name(hdr8, strtab, stroff):
    """Return the section name (bytes) for an 8-byte COFF section-name field."""
    if hdr8[0:1] == b"/" or hdr8[:4] == b"\x00\x00\x00\x00":
        # long name: "/<decimal offset into the string table>" (GNU as / MSVC),
        # or four zero bytes followed by the same offset (rare bigobj form)
        raw = hdr8[1:8] if hdr8[0:1] == b"/" else hdr8[4:8]
        try:
            off = int(raw.split(b"\x00")[0])
        except ValueError:
            return b""
        start = stroff + off
        end = strtab.find(b"\x00", start)
        if end < 0:
            return b""
        return strtab[start:end]
    return hdr8.rstrip(b"\x00")


def fix_object(path, verbose=False):
    with open(path, "rb") as fh:
        data = bytearray(fh.read())

    if len(data) < 20:
        return 0
    machine, nsec, _ts, symptr, nsym, optsz, _chars = struct.unpack_from(
        "<HHIIIHH", data, 0
    )
    if machine not in (0x014C, 0x8664):  # i386 / x86-64 COFF
        return 0
    if nsec == 0:
        return 0

    sec_off = 20 + optsz
    strtab_off = symptr + nsym * 18

    changed = 0
    for i in range(nsec):
        base = sec_off + i * 40
        if base + 40 > len(data):
            break
        name_field = bytes(data[base:base + 8])
        vsize, vaddr, rawsz, rawptr, relptr, lnptr, nrel, nln, chars = \
            struct.unpack_from("<IIIIIIHHI", data, base + 8)

        if nrel != 0 or rawsz == 0:
            continue

        name = section_name(name_field, data, strtab_off)
        if not name.startswith(b".data$"):
            continue
        if rawptr + rawsz > len(data):
            continue
        # every byte must be zero
        if data.count(0, rawptr, rawptr + rawsz) != rawsz:
            continue

        # The section name has to become a .bss* name so that the linker
        # merges it into the .bss output section: a PE output section cannot
        # contain a hole, so merging a zero-filled section into .data makes
        # the linker emit the zeros to disk again.
        # Long names live in the COFF string table, and ".data$" is one byte
        # longer than ".bss$", so the name cannot be patched in place.
        # Instead a short, unique name is written into the 8-byte field,
        # which always fits: ".bss$%d" is at most 8 bytes for the numbers we
        # need.  The old string-table entry simply becomes unreferenced.
        newname = (".bss$%d" % i).encode("ascii")
        if len(newname) > 8:
            newname = b".bss$~" + bytes([0x41 + (i % 26)])
        data[base:base + 8] = newname.ljust(8, b"\x00")

        # VirtualSize must keep the original size, otherwise the space the
        # symbols need would disappear:
        #   VirtualSize = raw size, SizeOfRawData = 0  ->  pure BSS
        struct.pack_into("<II", data, base + 8, max(vsize, rawsz), vaddr)
        struct.pack_into("<II", data, base + 16, 0, 0)          # rawsz, rawptr
        newchars = (chars & ~IMAGE_SCN_CNT_INITIALIZED_DATA) \
            | IMAGE_SCN_CNT_UNINITIALIZED_DATA
        struct.pack_into("<I", data, base + 36, newchars)

        changed += 1
        if verbose:
            print("    %-24s -> %-24s (%d bytes dropped)"
                  % (name.decode("latin1"), newname.decode("latin1"), rawsz))

    if changed:
        with open(path, "wb") as fh:
            fh.write(data)
    return changed


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2

    targets = []
    if argv[1] == "--recurse":
        root = argv[2]
        for dirpath, _dirs, files in os.walk(root):
            for fn in files:
                if fn.endswith(".o"):
                    targets.append(os.path.join(dirpath, fn))
    else:
        targets = [a for a in argv[1:] if a.endswith(".o")]

    total_secs = 0
    total_files = 0
    for path in targets:
        n = fix_object(path)
        if n:
            total_files += 1
            total_secs += n
    print("pe_bss_fix: %d object(s), %d section(s) moved to .bss"
          % (total_files, total_secs))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
