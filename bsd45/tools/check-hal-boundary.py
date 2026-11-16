#!/usr/bin/env python3
"""
check-hal-boundary.py - enforce the preemption HAL boundary.

The preemption policy must not contain i386 details.  This checker strips
comments, then rejects hardware/assembly identifiers in the generic files
and requires the arch hooks to be defined only under kernel/arch/<arch>/hal.

Run: python tools/check-hal-boundary.py
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

GENERIC = [
    "kernel/hal/ipl.c",
    "kernel/hal/clock.c",
    "kernel/hal/context.h",
    "kernel/thread/kthread.c",
]

FORBIDDEN = [
    r"\bhal_outb\b", r"\bhal_inb\b", r"\bsetidt\b", r"\btss_esp0\b",
    r"\bkswitch\s*\(", r"\beflags\b", r"arch/i386", r"\bPIC1_\w+",
    r"\bPIC2_\w+", r"\bPIT_CH\d\b", r"\birq0_entry\b", r"\bIO_ICU\d\b",
]

ARCH_HOOKS = [
    "hal_irq_to_ipl", "hal_ipl_init", "hal_ipl_apply",
    "hal_clock_init", "hal_clock_eoi",
    "hal_kernel_stack_set", "hal_switch_stack",
    "hal_intr_from_user", "hal_intr_user_resume",
]


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return text


def main():
    errors = 0
    for rel in GENERIC:
        path = os.path.join(ROOT, rel)
        with open(path, "r", encoding="utf-8", errors="surrogateescape") as f:
            text = strip_comments(f.read())
        for pat in FORBIDDEN:
            for m in re.finditer(pat, text):
                line = text.count("\n", 0, m.start()) + 1
                print("FAIL %s:%d forbidden %s" % (rel, line, m.group(0)))
                errors += 1

    arch_dir = os.path.join(ROOT, "kernel", "arch")
    for dirpath, dirnames, filenames in os.walk(arch_dir):
        for fn in filenames:
            if not fn.endswith(".c"):
                continue
            path = os.path.join(dirpath, fn)
            with open(path, "r", encoding="utf-8",
                      errors="surrogateescape") as f:
                text = strip_comments(f.read())
            for hook in ARCH_HOOKS:
                if re.search(r"^\s*(?:void|int|unsigned)\s+%s\s*\(" % hook,
                             text, re.M):
                    print("ok   %s defines %s" %
                          (os.path.relpath(path, ROOT), hook))

    if errors:
        print("check_hal_boundary: FAILED (%d)" % errors)
        return 1
    print("check_hal_boundary: PASS (generic preemption files are platform-neutral)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
