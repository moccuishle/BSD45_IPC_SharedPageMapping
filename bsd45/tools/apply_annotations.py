#!/usr/bin/env python3
"""
apply_annotations.py - insert the bilingual function comments produced by
the annotation workflow.

Input is one or more JSON files of the form

    [{"file": "kernel/fs/vfs.c",
      "functions": [{"name":"vfs_namei", "en":"...", "cn":"..."}]}]

The C parser from annotate_functions.py locates each definition; comments
are inserted immediately above its declaration, indented like the
declaration, and the pass is idempotent.  Code is never modified.
"""

import argparse
import glob
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from annotate_functions import find_functions, ROOT, BACKUP

MARK = "/* EN:"


def clean_text(s, what):
    s = " ".join(str(s).split())
    if not s:
        raise ValueError("empty %s" % what)
    if "*/" in s:
        s = s.replace("*/", "* /")
    if len(s) > 240:
        s = s[:237] + "..."
    return s


def apply_entry(entry):
    rel = entry["file"].replace("\\", "/")
    path = os.path.join(ROOT, rel)
    with open(path, "r", encoding="utf-8", errors="surrogateescape",
              newline="") as f:
        text = f.read()
    lines = text.splitlines(keepends=True)
    funcs = find_functions(text)
    by_name = {}
    for start, name in funcs:
        by_name.setdefault(name, []).append(start)

    wanted = []
    missing = []
    for fn in entry.get("functions", []):
        name = fn.get("name", "")
        if name not in by_name:
            missing.append(name)
            continue
        en = clean_text(fn.get("en", ""), name + ".en")
        cn = clean_text(fn.get("cn", ""), name + ".cn")
        for start in by_name[name]:
            wanted.append((start, name, en, cn))

    inserted = 0
    for start, name, en, cn in sorted(wanted, key=lambda x: x[0], reverse=True):
        # already annotated?
        ctx = "".join(lines[max(0, start - 4):start])
        if MARK in ctx:
            continue
        indent = re.match(r"[ \t]*", lines[start]).group(0)
        block = [indent + "/* EN: " + en + " */\n",
                 indent + "/* 中文：" + cn + " */\n"]
        lines[start:start] = block
        inserted += 1

    if inserted:
        os.makedirs(BACKUP, exist_ok=True)
        bak = os.path.join(BACKUP, rel)
        if not os.path.exists(bak):
            os.makedirs(os.path.dirname(bak), exist_ok=True)
        # the caller may run this twice; only back up the pristine file
        if not os.path.exists(bak):
            import shutil
            shutil.copy2(path, bak)
        with open(path, "w", encoding="utf-8", errors="surrogateescape",
                  newline="") as f:
            f.write("".join(lines))
    return inserted, len(wanted), missing


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("jsonfiles", nargs="+")
    args = ap.parse_args()
    files = []
    for pattern in args.jsonfiles:
        files.extend(glob.glob(pattern))
    total_ins = total_want = 0
    missing = []
    for jf in files:
        with open(jf, "r", encoding="utf-8") as f:
            data = json.load(f)
        if isinstance(data, dict):
            data = [data]
        for entry in data:
            ins, want, miss = apply_entry(entry)
            total_ins += ins
            total_want += want
            for m in miss:
                missing.append((entry["file"], m))
    print("apply_annotations: inserted %d/%d, missing %d"
          % (total_ins, total_want, len(missing)))
    for path, name in missing[:40]:
        print("  missing: %s :: %s" % (path, name))
    return 0 if total_want else 1


if __name__ == "__main__":
    sys.exit(main())
