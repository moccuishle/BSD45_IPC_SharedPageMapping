#!/usr/bin/env python3
"""Print the source of selected function definitions for annotators.

    python tools/fn_show.py --file kernel/fs/vfs.c --names vfs_namei,vfs_create
    python tools/fn_show.py --file kernel/fs/vfs.c          # all functions
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from annotate_functions import find_functions, ROOT


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--file", required=True)
    ap.add_argument("--names", default="")
    args = ap.parse_args()
    path = args.file if os.path.isabs(args.file) else os.path.join(ROOT, args.file)
    want = set(n for n in args.names.split(",") if n)
    with open(path, "r", encoding="utf-8", errors="surrogateescape",
              newline="") as f:
        text = f.read()
    lines = text.splitlines()
    funcs = sorted(find_functions(text), key=lambda x: x[0])
    for i, (start, name) in enumerate(funcs):
        if want and name not in want:
            continue
        end = funcs[i + 1][0] if i + 1 < len(funcs) else len(lines)
        print("===== %s:%d %s =====" % (args.file, start + 1, name))
        print("\n".join(lines[start:end]))
        print()


if __name__ == "__main__":
    main()
