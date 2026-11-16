#!/usr/bin/env python3
"""
fn_inventory.py - emit the function-definition inventory used by the
annotation workflow.

    python tools/fn_inventory.py --files kernel/hal/ipl.c kernel/hal/clock.c
    python tools/fn_inventory.py --all > build/fn_inventory.json

Each entry carries the file, function name, 1-based declaration line and a
short signature/body preview so an annotator can locate the code without
re-parsing the C file.
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from annotate_functions import find_functions, ROOT, KERNEL


def inventory(path):
    with open(path, "r", encoding="utf-8", errors="surrogateescape",
              newline="") as f:
        text = f.read()
    lines = text.splitlines()
    funcs = sorted(find_functions(text), key=lambda x: x[0])
    out = []
    for idx, (start, name) in enumerate(funcs):
        end = funcs[idx + 1][0] if idx + 1 < len(funcs) else len(lines)
        preview = "\n".join(lines[start:min(start + 10, end)])
        out.append({
            "name": name,
            "start": start + 1,
            "end": end,
            "preview": preview,
        })
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--files", nargs="*", default=[])
    ap.add_argument("--all", action="store_true")
    args = ap.parse_args()
    paths = []
    if args.all:
        for dirpath, dirnames, filenames in os.walk(KERNEL):
            for fn in sorted(filenames):
                if fn.endswith(".c"):
                    paths.append(os.path.join(dirpath, fn))
    else:
        for p in args.files:
            if not os.path.isabs(p):
                p = os.path.join(ROOT, p)
            paths.append(p)
    data = []
    for p in paths:
        rel = os.path.relpath(p, ROOT).replace("\\", "/")
        data.append({"file": rel, "functions": inventory(p)})
    json.dump(data, sys.stdout, ensure_ascii=False, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
