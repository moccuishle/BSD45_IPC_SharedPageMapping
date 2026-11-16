#!/usr/bin/env python3
"""
make_install_ffs.py - build the FFS image that the in-guest `install` command
writes to the VMware virtual disk (placed on the ISO as BSD45.FFS).

This is a thin wrapper over mkffs.py: it selects the userland programs that
/sbin/init needs after a disk boot and passes them to mkffs.py via a spec
file (so no leading-'/' arguments are rewritten by MSYS/Git Bash).  All
on-disk format logic lives in mkffs.py alone.

Usage:
    python3 make_install_ffs.py [output.img]
Default output: bsd45/hdd/BSD45.FFS (3 MiB).
"""

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
WS = os.path.dirname(HERE)
USERLAND = os.path.join(WS, "out", "userland")

# dest-on-disk -> userland ELF basename
FILES = [
    ("/sbin/init", "init.elf"),
    ("/bin/sh", "sh.elf"),
    ("/hello", "hello.elf"),
]

# optional core tools (included only when built)
TOOLS = ["echo", "ls", "cat", "mkdir", "rm", "cp", "mv", "wc", "grep",
         "head", "tail", "df", "mount", "ps", "uname", "sleep", "kill",
         "true", "false", "date", "vmstat", "test"]


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "BSD45.FFS")

    specs = []
    for dest, name in FILES:
        src = os.path.join(USERLAND, name)
        if os.path.exists(src):
            specs.append((dest, src))
        else:
            print(f"make_install_ffs: WARNING missing {src}", file=sys.stderr)
    for name in TOOLS:
        src = os.path.join(USERLAND, name + ".elf")
        if os.path.exists(src):
            specs.append(("/bin/" + name, src))

    if not any(d == "/bin/sh" for d, _ in specs) or not any(
            d == "/sbin/init" for d, _ in specs):
        print("make_install_ffs: init.elf or sh.elf missing in out/userland",
              file=sys.stderr)
        return 2

    # Write specs to a file so leading '/' paths are never argv-rewritten.
    fd, spec_path = tempfile.mkstemp(prefix="bsd45_ffs_", suffix=".txt",
                                     text=True)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            for dest, src in specs:
                f.write(f"{dest}={src}\n")
        cmd = [sys.executable, os.path.join(HERE, "mkffs.py"),
               "-o", out, "-size", str(3 * 1024 * 1024),
               "--spec-file", spec_path]
        return subprocess.call(cmd)
    finally:
        try:
            os.unlink(spec_path)
        except OSError:
            pass


if __name__ == '__main__':
    sys.exit(main())
