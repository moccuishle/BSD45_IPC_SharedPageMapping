#!/usr/bin/env python3
#
# Copyright (c) 2026 jialiang.  All rights reserved.
#

"""Generate a README.md for every source directory that has a config.mk.

The BSD45 tree carries the complete 4.5BSD source layout: each leaf directory
has a `config.mk` that records the sources and subdirectories of that part of
the tree.  This tool writes a short English-then-Chinese README.md describing
what the directory is responsible for; it never overwrites an existing
README.md.

Usage:
    python3 tools/gen_folder_readmes.py [--root bsd45] [--dry-run]
"""

import argparse
import os
import re
import sys

SKIP_DIRS = {"build", "out", ".git"}

# path prefix -> (English, Chinese) description
PREFIX = {
    "kernel/arch": ("architecture-specific machine-dependent code for the 4.5BSD kernel",
                    "4.5BSD 内核的机器相关代码"),
    "kernel/fs": ("the file-system layer of the 4.5BSD kernel",
                  "4.5BSD 内核的文件系统层"),
    "kernel/net": ("the networking protocol stacks of the 4.5BSD kernel",
                   "4.5BSD 内核的网络协议栈"),
    "kernel/vm": ("the virtual-memory subsystem of the 4.5BSD kernel",
                  "4.5BSD 内核的虚拟内存子系统"),
    "kernel/sys": ("the machine-independent kernel core (syscalls, processes, signals, tty)",
                   "与机器无关的内核核心（系统调用、进程、信号、tty）"),
    "kernel/lib": ("kernel library routines (libkern-style)",
                   "内核库函数（libkern 风格）"),
    "kernel/driver": ("device drivers used by this port",
                      "本端口使用的设备驱动"),
    "kernel/include": ("kernel headers shared by the 4.5BSD source tree",
                       "4.5BSD 源码树共享的内核头文件"),
    "kernel/other": ("the BSD45 port glue (boot, monitor, syscall bridge)",
                     "BSD45 端口的 glue 代码（启动、monitor、系统调用桥）"),
    "libc/arch/bsd": ("the 4.5BSD userland sources used by this port's /bin/sh and libc",
                 "本端口 /bin/sh 与 libc 使用的 4.5BSD 用户态源码"),
    "libexec": ("userland tools built for BSD45 (one main() per file)",
                "为 BSD45 构建的用户态工具（每个文件一个 main()）"),
    "libc/src": ("the port's small libc / syscall glue",
                 "本端口的小型 libc 与系统调用粘合层"),
    "tools": ("build, test and maintenance scripts for BSD45",
              "BSD45 的构建、测试与维护脚本"),
    "boot": ("the i386 boot chain (stage1/stage2) and linker script",
             "i386 引导链（stage1/stage2）与链接脚本"),
    "rootfs_overlay": ("files copied on top of the ISO root (etc, readme)",
                       "覆盖到 ISO 根目录的文件（etc、readme）"),
    "docs": ("project documentation, design notes and recovery guides",
             "项目文档、设计说明与恢复指南"),
}

# exact relative directory -> (English, Chinese)
EXACT = {
    "kernel": ("the 4.5BSD kernel source tree (sys, vm, fs, net, arch, drivers)",
               "4.5BSD 内核源码树（sys、vm、fs、net、arch、驱动）"),
    "kernel/fs/vfs_": ("", ""),
    "libc": ("the userland C library and 4.5BSD userland sources",
             "用户态 C 库与 4.5BSD 用户态源码"),
    "libc/arch/bsd/bin/sh": ("the 4.5BSD Bourne shell compiled as an i386 ELF for BSD45",
                        "编译成 i386 ELF 的 4.5BSD Bourne shell"),
    "libc/arch/bsd/lib/libc": ("the 4.5BSD libc implementation subset used by the shell",
                          "shell 使用的 4.5BSD libc 实现子集"),
    "libc/arch/bsd/include": ("4.5BSD public headers",
                         "4.5BSD 公共头文件"),
    "libc/arch/bsd/sys/sys": ("4.5BSD kernel/user shared sys headers",
                         "4.5BSD 内核与用户态共享的 sys 头文件"),
    "libc/arch/bsd/sys/i386/include": ("4.5BSD i386 machine headers",
                                  "4.5BSD i386 机器相关头文件"),
    "libc/arch/bsd/sys/net": ("4.5BSD network headers",
                         "4.5BSD 网络头文件"),
    "libc/arch/bsd/usr.bin/printf": ("the printf(1) implementation from 4.5BSD",
                                "4.5BSD 的 printf(1) 实现"),
    "kernel/arch/i386/isa": ("ISA bus and PC-compatible hardware support (PIC, PIT, keyboard, serial, ATAPI)",
                             "ISA 总线与 PC 兼容硬件支持（PIC、PIT、键盘、串口、ATAPI）"),
    "kernel/arch/i386/isa/ic": ("ISA controller chips (8259/8253/8042 style helpers)",
                                "ISA 控制器芯片（8259/8253/8042 风格辅助代码）"),
    "kernel/arch/i386/i386": ("i386 machine-dependent core (locore, pmap, trap, cons)",
                              "i386 机器相关核心（locore、pmap、trap、cons）"),
    "kernel/arch/i386/eisa": ("EISA bus support",
                              "EISA 总线支持"),
    "kernel/arch/i386/pci": ("PCI bus support",
                             "PCI 总线支持"),
    "kernel/arch/i386/mca": ("MCA bus support",
                             "MCA 总线支持"),
    "kernel/arch/i386/include": ("i386 machine headers",
                                 "i386 机器相关头文件"),
    "kernel/arch/i386/conf": ("i386 kernel configuration files",
                              "i386 内核配置文件"),
    "kernel/arch/i386/stand": ("i386 standalone boot code",
                               "i386 独立引导代码"),
    "kernel/arch/i386/stand/libsa": ("standalone library used by the i386 boot loader",
                                     "i386 引导器使用的独立库"),
    "kernel/fs/isofs": ("ISO9660 (CD-ROM) file-system support",
                        "ISO9660（光盘）文件系统支持"),
    "kernel/fs/tmpfs": ("the in-memory writable file system mounted on /tmp",
                        "挂载在 /tmp 的内存可写文件系统"),
    "kernel/fs/ufs": ("the Berkeley UFS/FFS file-system family",
                      "Berkeley UFS/FFS 文件系统族"),
    "kernel/fs/ufs/ffs": ("the Fast File System implementation",
                          "快速文件系统（FFS）实现"),
    "kernel/fs/ufs/lfs": ("the Log-structured File System implementation",
                          "日志结构文件系统（LFS）实现"),
    "kernel/fs/ufs/mfs": ("the memory-backed file system used for swap",
                          "用于交换区的内存文件系统"),
    "kernel/fs/miscfs": ("pseudo file systems (procfs, kernfs, fdesc, nullfs, union, ...)",
                         "伪文件系统（procfs、kernfs、fdesc、nullfs、union 等）"),
    "kernel/fs/nfs": ("the NFS client/server implementation",
                      "NFS 客户端/服务端实现"),
    "kernel/fs/msdosfs": ("MS-DOS FAT file-system support",
                          "MS-DOS FAT 文件系统支持"),
    "kernel/fs/cd9660": ("the alternative ISO9660 implementation",
                         "另一份 ISO9660 实现"),
    "kernel/net/netinet": ("the TCP/IP protocol family (IP, ICMP, TCP, UDP)",
                           "TCP/IP 协议族（IP、ICMP、TCP、UDP）"),
    "kernel/net/netiso": ("the ISO/OSI protocol family (CLNP, TP4, ...)",
                          "ISO/OSI 协议族（CLNP、TP4 等）"),
    "kernel/net/netns": ("the Xerox NS protocol family",
                         "Xerox NS 协议族"),
    "kernel/net/netccitt": ("the CCITT X.25 protocol family",
                            "CCITT X.25 协议族"),
    "kernel/net/netatalk": ("the AppleTalk protocol family",
                            "AppleTalk 协议族"),
    "kernel/net/kern": ("network kernel support (routing, mbufs)",
                        "网络内核支持（路由、mbuf）"),
    "kernel/net/iso": ("ISO transport support shared by the network stack",
                       "网络栈共享的 ISO 传输支持"),
    "kernel/driver/dev": ("block/character device drivers (SCSI CD, vnode disk, ...)",
                          "块/字符设备驱动（SCSI CD、vnode 磁盘等）"),
    "kernel/driver/dev/scsi": ("SCSI support used by the CD driver",
                               "CD 驱动使用的 SCSI 支持"),
    "kernel/vm": ("virtual memory: maps, objects, pages, pagers, faults",
                  "虚拟内存：映射、对象、页、pager、缺页"),
    "kernel/sys": ("kernel core: init, processes, signals, descriptors, locks, syscalls",
                   "内核核心：初始化、进程、信号、描述符、锁、系统调用"),
    "kernel/sys/i386": ("i386-specific kernel syscall glue",
                        "i386 相关内核系统调用 glue"),
    "kernel/lib/libkern": ("portable kernel utility routines",
                           "可移植的内核工具函数"),
}

SUBDIR_KIND = {
    "conf": ("kernel configuration files", "内核配置文件"),
    "dev": ("device drivers", "设备驱动"),
    "include": ("header files", "头文件"),
    "stand": ("standalone boot code", "独立引导代码"),
    "libsa": ("standalone library", "独立库"),
    "isa": ("ISA bus support", "ISA 总线支持"),
    "eisa": ("EISA bus support", "EISA 总线支持"),
    "pci": ("PCI bus support", "PCI 总线支持"),
    "mca": ("MCA bus support", "MCA 总线支持"),
    "scripts": ("build/configuration scripts", "构建/配置脚本"),
    "doc": ("documentation", "文档"),
    "DOC": ("documentation", "文档"),
    "test": ("tests", "测试"),
    "tests": ("tests", "测试"),
    "stand.att": ("AT&T-derived standalone code", "AT&T 派生的独立代码"),
    "compile": ("compiler support", "编译器支持"),
    "conf": ("configuration", "配置"),
}


def describe(rel):
    rel = rel.replace(os.sep, "/").strip("/")
    if rel in EXACT and EXACT[rel][0]:
        return EXACT[rel]
    best = None
    for prefix in sorted(PREFIX, key=len, reverse=True):
        if rel == prefix or rel.startswith(prefix + "/"):
            best = prefix
            break
    if best:
        en, zh = PREFIX[best]
        tail = rel[len(best):].lstrip("/")
        if tail:
            parts = tail.split("/")
            last = parts[-1]
            kind = SUBDIR_KIND.get(last)
            if kind:
                en = en + " (" + kind[0] + ")"
                zh = zh + "（" + kind[1] + "）"
            else:
                en = en + " — " + tail
                zh = zh + " — " + tail
        return en, zh
    return ("part of the BSD45/4.5BSD source tree", "BSD45/4.5BSD 源码树的一部分")


def parse_config(path):
    sources = []
    subdirs = []
    mode = None
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                line = line.rstrip("\n")
                if line.startswith("# sources in this directory:"):
                    mode = "src"
                    continue
                if line.startswith("# subdirectories:"):
                    mode = "dir"
                    continue
                if mode and line.startswith("#   "):
                    item = line[4:].strip()
                    if item:
                        (sources if mode == "src" else subdirs).append(item)
                    continue
                if mode and line and not line.startswith("#"):
                    mode = None
    except OSError:
        pass
    return sources, subdirs


def make_readme(rel, sources, subdirs):
    en, zh = describe(rel)
    title_en = rel if rel else "."
    lines = []
    lines.append("# `%s` — %s\n" % (title_en, en))
    lines.append("<!-- generated by tools/gen_folder_readmes.py -->\n")
    lines.append("## English\n")
    lines.append("This directory contains %s.\n" % en)
    if sources:
        shown = ", ".join("`%s`" % s for s in sources[:12])
        more = "" if len(sources) <= 12 else ", ..."
        lines.append("`config.mk` records %d source file(s) here: %s%s.\n" %
                     (len(sources), shown, more))
    else:
        lines.append("`config.mk` records no directly compiled sources here; "
                     "the work is in the subdirectories.\n")
    if subdirs:
        shown = ", ".join("`%s`" % s for s in subdirs)
        lines.append("Subdirectories: %s.\n" % shown)
    lines.append("These `config.mk` lists are reference metadata from the 4.5BSD "
                 "tree; the BSD45 build scripts (`build_kernel.ps1` / "
                 "`build_kernel.sh`) select the files that are actually compiled.\n")
    lines.append("## 中文\n")
    lines.append("本目录包含%s。\n" % zh)
    if sources:
        shown = "、".join("`%s`" % s for s in sources[:12])
        more = "" if len(sources) <= 12 else " 等"
        lines.append("`config.mk` 记录了 %d 个源文件：%s%s。\n" %
                     (len(sources), shown, more))
    else:
        lines.append("`config.mk` 未记录直接编译的源文件，实际工作在其子目录中。\n")
    if subdirs:
        shown = "、".join("`%s`" % s for s in subdirs)
        lines.append("子目录：%s。\n" % shown)
    lines.append("这些 `config.mk` 清单来自 4.5BSD 源码树，是参考元数据；BSD45 的"
                 "构建脚本（`build_kernel.ps1` / `build_kernel.sh`）会显式选择"
                 "实际参与编译的文件。\n")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.path.join(os.path.dirname(__file__), ".."))
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    root = os.path.abspath(args.root)
    made = skipped = 0
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        if "config.mk" not in filenames:
            continue
        rel = os.path.relpath(dirpath, root)
        readme = os.path.join(dirpath, "README.md")
        if os.path.exists(readme):
            skipped += 1
            continue
        sources, subdirs = parse_config(os.path.join(dirpath, "config.mk"))
        text = make_readme(rel, sources, subdirs)
        if args.dry_run:
            print("would write", os.path.join(rel, "README.md"))
        else:
            with open(readme, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
        made += 1
    print("README.md written: %d, skipped (already exists): %d, root: %s" %
          (made, skipped, root))
    return 0


if __name__ == "__main__":
    sys.exit(main())
