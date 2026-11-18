#!/usr/bin/env python3
"""make_install_jlfs.py - build a native-JLFS install image (3 MiB) for BSD45.

JLFS counterpart of the original make_install_ffs.py: same userland selection,
same 3 MiB layout, but calls mkjlfs.py instead of mkffs.py.  The resulting
image has a native JLFS cylinder-group bitmap, so jlfs_mount() mounts it
read-write without the "rebuilding allocation bitmap" scan.

The original make_install_ffs.py / mkffs.py are NOT modified.

Usage: make_install_jlfs.py [output.img]   (default: hdd/BSD45.JLFS)
"""

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
WS = os.path.dirname(HERE)
USERLAND = os.path.join(WS, "out", "userland")

FILES = [
    ("/sbin/init", "init.elf"),
    ("/bin/sh", "sh.elf"),
    ("/hello", "hello.elf"),
]
TOOLS = ["echo", "ls", "cat", "mkdir", "rm", "cp", "mv", "wc", "grep",
         "head", "tail", "df", "mount", "ps", "uname", "sleep", "kill",
         "true", "false", "date", "vmstat", "test"]


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "BSD45.JLFS")
    specs = []
    for dest, name in FILES:
        src = os.path.join(USERLAND, name)
        if os.path.exists(src):
            specs.append((dest, src))
        else:
            print(f"make_install_jlfs: WARNING missing {src}", file=sys.stderr)
    for name in TOOLS:
        src = os.path.join(USERLAND, name + ".elf")
        if os.path.exists(src):
            specs.append(("/bin/" + name, src))
    if not any(d == "/bin/sh" for d, _ in specs) or not any(
            d == "/sbin/init" for d, _ in specs):
        print("make_install_jlfs: init.elf or sh.elf missing in out/userland",
              file=sys.stderr)
        return 2

    fd, spec_path = tempfile.mkstemp(prefix="bsd45_jlfs_", suffix=".txt", text=True)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            for dest, src in specs:
                f.write(f"{dest}={src}\n")
        return subprocess.call([sys.executable, os.path.join(HERE, "mkjlfs.py"),
                                "-o", out, "-size", str(3 * 1024 * 1024),
                                "--spec-file", spec_path])
    finally:
        try:
            os.unlink(spec_path)
        except OSError:
            pass


if __name__ == '__main__':
    sys.exit(main())
