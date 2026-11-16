#!/usr/bin/env python3
"""
annotate_functions.py - add a two-line (English + Chinese) comment above
every C function definition in bsd45/kernel.

The 4.5BSD tree is ~238 C files and several thousand K&R-style function
definitions, so this is a mechanical pass.  The comments are generated from
the function name, its file and a small domain/verb table; the text makes no
claim that cannot be checked from the name, and the pass is idempotent
(files already carrying the "/* EN:" marker are skipped).

Usage:
    python tools/annotate_functions.py --dry-run
    python tools/annotate_functions.py --apply
    python tools/annotate_functions.py --apply --only kernel/hal
"""

import argparse
import os
import re
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KERNEL = os.path.join(ROOT, "kernel")
BACKUP = os.path.join(ROOT, "build", "annot_backup")
MARK = "/* EN:"

TYPE_WORDS = {
    "static", "extern", "register", "inline", "__inline", "__inline__",
    "const", "volatile", "__const", "__volatile", "unsigned", "signed",
    "long", "short", "struct", "union", "enum", "void", "int", "char",
    "float", "double", "_Bool", "size_t", "ssize_t", "off_t", "pid_t",
    "uid_t", "gid_t", "time_t", "caddr_t", "daddr_t", "dev_t", "ino_t",
    "u_int", "u_long", "u_short", "u_char", "u_quad_t", "quad_t",
    "vm_offset_t", "vm_size_t", "vm_prot_t", "boolean_t", "natural_t",
    "integer_t", "kern_return_t", "io_req_t", "task_t", "thread_t",
}

DOMAIN = [
    (("vfs_", "namei", "vn_", "vnode", "dentry"), "VFS", "VFS"),
    (("ufs", "ffs", "nfs", "tmpfs", "isofs", "cnfs", "fat", "exfat", "ntfs",
      "zfs"), "filesystem", "文件系统"),
    (("pmap", "vm_", "page", "pv_"), "VM/pmap", "VM/pmap"),
    (("obj_", "object", "handle", "unistr", "kstr"), "object-manager",
     "对象管理器"),
    (("thread", "kthread", "sched", "kswitch", "context"), "scheduler",
     "调度器"),
    (("io_", "ioreq", "iocp", "irp", "aio"), "I/O", "I/O"),
    (("tcp", "udp", "ip_", "icmp", "if_", "route", "bpf", "sock", "net"),
     "network", "网络"),
    (("registry", "reg_"), "registry", "注册表"),
    (("sync", "lock", "qspin", "pushlock", "eres"), "synchronization", "同步"),
    (("sig", "signal", "kill", "sendsig", "psignal"), "signal", "信号"),
    (("atapi", "ata", "pci", "isa", "com", "fd_", "wd_", "cons", "pccon",
      "clock", "cpu", "hal_", "ipl", "irq"), "HAL/driver", "HAL/驱动"),
    (("sys_", "kern_", "do_", "fork", "exec", "exit", "wait"), "kernel",
     "内核"),
]

VERB = [
    (("init", "setup", "bootstrap", "start", "open"), "initializes state",
     "初始化状态"),
    (("alloc", "new", "get", "grab", "ref"), "obtains or allocates an object",
     "获取或分配对象"),
    (("free", "put", "release", "destroy", "delete", "remove", "drop",
      "close"), "releases or drops an object", "释放或丢弃对象"),
    (("read", "write", "copy", "move", "copyin", "copyout", "output",
      "input"), "moves data", "搬运数据"),
    (("lookup", "namei", "find", "search", "resolve", "create", "make"),
     "resolves or creates a name", "解析或创建名字"),
    (("map", "unmap", "enter", "extract", "protect"), "updates mappings",
     "更新映射"),
    (("lock", "unlock", "acquire", "trylock", "spin"), "synchronizes access",
     "同步访问"),
    (("sleep", "wakeup", "block", "wait", "pause"), "blocks or wakes a thread",
     "阻塞或唤醒线程"),
    (("fork", "exec", "exit", "spawn"), "handles process lifecycle",
     "处理进程生命周期"),
    (("print", "dump", "show", "report", "stats", "stat", "trace", "log"),
     "reports state", "报告状态"),
    (("check", "test", "verify", "probe", "validate", "assert", "selftest"),
     "checks state or invariants", "检查状态或不变量"),
    (("set", "update", "adjust", "flush", "sync", "clear", "reset"),
     "updates state", "更新状态"),
    (("dispatch", "handle", "process", "serve", "run", "call"), "dispatches work",
     "分派工作"),
]


def words(name):
    name = name.lstrip("_")
    parts = re.sub(r"([a-z0-9])([A-Z])", r"\1 \2", name)
    parts = re.split(r"[^A-Za-z0-9]+", parts)
    return [p.lower() for p in parts if p]


def classify(name):
    low = name.lower()
    dom_en, dom_cn = "kernel", "内核"
    for keys, en, cn in DOMAIN:
        if any(k in low for k in keys):
            dom_en, dom_cn = en, cn
            break
    verb_en, verb_cn = "implements the named operation", "实现同名操作"
    for keys, en, cn in VERB:
        if any(k in low for k in keys):
            verb_en, verb_cn = en, cn
            break
    return dom_en, dom_cn, verb_en, verb_cn


def describe(name, path):
    dom_en, dom_cn, verb_en, verb_cn = classify(name)
    if verb_en == "implements the named operation":
        base = os.path.basename(path)
        en = "%s: %s routine in %s." % (name, dom_en, base)
        cn = "%s：%s 中的%s例程。" % (name, base, dom_cn)
    else:
        en = "%s: %s routine that %s." % (name, dom_en, verb_en)
        cn = "%s：%s例程，%s。" % (name, dom_cn, verb_cn)
    en = en.replace("*/", "* /")
    cn = cn.replace("*/", "* /")
    return en, cn


def strip_comments_strings(text):
    """Blank out comments and literals while keeping offsets/newlines."""
    out = list(text)
    i, n = 0, len(text)
    state = "code"
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if state == "code":
            if c == "/" and nxt == "*":
                state = "block"
                out[i] = out[i + 1] = " "
                i += 2
                continue
            if c == "/" and nxt == "/":
                state = "line"
                out[i] = out[i + 1] = " "
                i += 2
                continue
            if c == '"':
                state = "str"
                out[i] = " "
            elif c == "'":
                state = "char"
                out[i] = " "
        elif state == "block":
            if c == "*" and nxt == "/":
                out[i] = out[i + 1] = " "
                state = "code"
                i += 2
                continue
            if c != "\n":
                out[i] = " "
        elif state == "line":
            if c == "\n":
                state = "code"
            else:
                out[i] = " "
        elif state in ("str", "char"):
            if c == "\\":
                out[i] = " "
                if i + 1 < n and text[i + 1] != "\n":
                    out[i + 1] = " "
                i += 2
                continue
            if (state == "str" and c == '"') or (state == "char" and c == "'"):
                out[i] = " "
                state = "code"
            elif c != "\n":
                out[i] = " "
        i += 1
    return "".join(out)


def find_functions(text):
    """Yield (insert_line, name) for top-level definitions."""
    clean = strip_comments_strings(text)
    lines = text.splitlines(keepends=True)
    offsets = []
    pos = 0
    for line in lines:
        offsets.append(pos)
        pos += len(line)

    def line_of(off):
        # binary search
        lo, hi = 0, len(offsets) - 1
        while lo < hi:
            mid = (lo + hi + 1) // 2
            if offsets[mid] <= off:
                lo = mid
            else:
                hi = mid - 1
        return lo

    found = []
    for li, line in enumerate(lines):
        s = line.lstrip()
        if not s.startswith("{"):
            continue
        brace = offsets[li] + (len(line) - len(s))
        if clean[brace] != "{":
            continue
        # Junk after the brace on the same line is not a plain function body.
        rest = clean[brace + 1:offsets[li] + len(line)].strip()
        if rest and not rest.startswith("/*"):
            # allow "{ }" and attribute suffixes only
            if not re.match(r"^[};]*$", rest):
                continue
        # scan back for the matching ')' of the parameter list
        j = brace - 1
        depth = 0
        close = -1
        while j >= 0:
            c = clean[j]
            if c == "}":
                break
            if c == "{":
                break
            if c == ")":
                if depth == 0 and close < 0:
                    close = j
                depth += 1
            elif c == "(":
                if depth == 0:
                    break
                depth -= 1
            elif c == ";" and depth == 0:
                # K&R parameter declarations sit between ')' and '{'
                pass
            j -= 1
        if close < 0:
            continue
        # find matching '('
        j = close - 1
        depth = 0
        op = -1
        while j >= 0:
            c = clean[j]
            if c == ")":
                depth += 1
            elif c == "(":
                if depth == 0:
                    op = j
                    break
                depth -= 1
            elif c in ";{}" and depth == 0:
                break
            j -= 1
        if op < 0:
            continue
        # name token before '('
        k = op - 1
        while k >= 0 and clean[k] in " \t\r\n":
            k -= 1
        end = k + 1
        while k >= 0 and (clean[k].isalnum() or clean[k] == "_"):
            k -= 1
        name = clean[k + 1:end]
        if not name or not re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", name):
            continue
        if name in ("if", "for", "while", "switch", "sizeof", "return",
                    "do", "else"):
            continue
        # no statement/assignment on the declaration line
        nl = line_of(op)
        head = clean[offsets[nl]:op]
        if "=" in head or ";" in head or "#" in head:
            continue

        # walk up over the return type to the declaration start
        start = nl
        while start - 1 >= 0:
            prev = lines[start - 1]
            ps = prev.strip()
            if ps == "":
                break
            if ps.startswith("/*") or ps.startswith("*") or ps.endswith("*/"):
                break
            if ps.startswith("#"):
                break
            if ps.endswith(";") or ps.endswith("}") or ps.endswith("{"):
                break
            toks = re.sub(r"[*()\[\]]", " ", ps).split()
            if toks and all(t in TYPE_WORDS or re.match(r"^[A-Za-z_]\w*$", t)
                            for t in toks):
                # a bare identifier is ambiguous; only accept known type words
                if any(t in TYPE_WORDS for t in toks) or ps.endswith("*"):
                    start -= 1
                    continue
            break
        # skip definitions already annotated
        ctx = "".join(lines[max(0, start - 4):start])
        if MARK in ctx:
            continue
        found.append((start, name))
    # de-duplicate (same insertion line)
    seen = set()
    out = []
    for start, name in found:
        if start in seen:
            continue
        seen.add(start)
        out.append((start, name))
    return out


def annotate_file(path, apply_changes):
    with open(path, "r", encoding="utf-8", errors="surrogateescape",
              newline="") as f:
        text = f.read()
    funcs = find_functions(text)
    if not funcs:
        return 0
    lines = text.splitlines(keepends=True)
    # insert bottom-up so indices stay valid
    inserted = 0
    for start, name in sorted(funcs, key=lambda x: x[0], reverse=True):
        en, cn = describe(name, path)
        indent = re.match(r"[ \t]*", lines[start]).group(0)
        block = [indent + "/* EN: " + en + " */\n",
                 indent + "/* 中文：" + cn + " */\n"]
        # keep a blank line between an existing doc comment and our lines
        lines[start:start] = block
        inserted += 1
    if apply_changes and inserted:
        os.makedirs(BACKUP, exist_ok=True)
        rel = os.path.relpath(path, ROOT)
        bak = os.path.join(BACKUP, rel)
        if not os.path.exists(bak):
            os.makedirs(os.path.dirname(bak), exist_ok=True)
            shutil.copy2(path, bak)
        with open(path, "w", encoding="utf-8", errors="surrogateescape",
                  newline="") as f:
            f.write("".join(lines))
    return inserted


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--only", default="")
    args = ap.parse_args()
    apply_changes = args.apply and not args.dry_run

    total_files = total_funcs = changed_files = 0
    for dirpath, dirnames, filenames in os.walk(KERNEL):
        for fn in sorted(filenames):
            if not fn.endswith(".c"):
                continue
            path = os.path.join(dirpath, fn)
            if args.only:
                rel = os.path.relpath(path, ROOT).replace("\\", "/")
                if args.only.replace("\\", "/") not in rel:
                    continue
            n = annotate_file(path, apply_changes)
            total_files += 1
            total_funcs += n
            if n:
                changed_files += 1
    mode = "APPLIED" if apply_changes else "DRY-RUN"
    print("%s: %d C files, %d annotations, %d files changed"
          % (mode, total_files, total_funcs, changed_files))
    return 0


if __name__ == "__main__":
    sys.exit(main())
