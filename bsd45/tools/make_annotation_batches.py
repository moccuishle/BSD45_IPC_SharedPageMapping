#!/usr/bin/env python3
"""Build the annotation workflow batches from tools/fn_inventory.py output."""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from annotate_functions import KERNEL, ROOT, find_functions

MAX_FUNCS = 30
batches = []
cur = {"id": "b%03d" % 0, "files": []}
cur_count = 0
batch_no = 0

for dirpath, dirnames, filenames in os.walk(KERNEL):
    for fn in sorted(filenames):
        if not fn.endswith(".c"):
            continue
        path = os.path.join(dirpath, fn)
        rel = os.path.relpath(path, ROOT).replace("\\", "/")
        with open(path, "r", encoding="utf-8", errors="surrogateescape",
                  newline="") as f:
            text = f.read()
        names = [n for _, n in find_functions(text)]
        if not names:
            continue
        i = 0
        while i < len(names):
            room = MAX_FUNCS - cur_count
            if room <= 0:
                batches.append(cur)
                batch_no += 1
                cur = {"id": "b%03d" % batch_no, "files": []}
                cur_count = 0
                room = MAX_FUNCS
            take = names[i:i + room]
            cur["files"].append({"path": rel, "names": take})
            cur_count += len(take)
            i += len(take)
if cur["files"]:
    batches.append(cur)

os.makedirs(os.path.join(ROOT, "build", "ann"), exist_ok=True)
with open(os.path.join(ROOT, "build", "ann", "batches.json"), "w",
          encoding="utf-8") as f:
    json.dump(batches, f, ensure_ascii=False, indent=1)

# four workflow argument files
n = len(batches)
groups = [batches[i * n // 4:(i + 1) * n // 4] for i in range(4)]
for gi, g in enumerate(groups):
    with open(os.path.join(ROOT, "build", "ann", "group%d.json" % gi), "w",
              encoding="utf-8") as f:
        json.dump({"batches": g}, f, ensure_ascii=False)
    print("group%d: %d batches, %d functions" %
          (gi, len(g), sum(sum(len(x["names"]) for x in b["files"]) for b in g)))
print("total: %d batches, %d functions" %
      (n, sum(sum(len(x["names"]) for x in b["files"]) for b in batches)))
