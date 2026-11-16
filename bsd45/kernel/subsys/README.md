<!-- Copyright (c) 2026 jialiang.  All rights reserved. -->

# `kernel/subsys` — API personalities decoupled from the kernel core

## English

The kernel exports one internal service ABI (`bsd45_syscall()`, the native
4.5BSD call numbers).  A personality translates an external API into that ABI
without touching kernel internals:

- `subsys.c` — registry, `set_personality`/`get_personality` (424/425) and
  dispatch to the active personality.
- `subsys_unix.c` — the native 4.5BSD ABI (default).
- `subsys_posix.c` — the POSIX surface, with explicit call filtering.
- `subsys_linux.c` — Linux i386 call numbers translated to native numbers
  (`mkdir 39→136`, `wait4 114→7`, `mmap2 192→197`, `exit_group 252→exit`).
- `subsys_win32.c` — a minimal Win32 handle API (GetCurrentProcessId,
  CreateFileA, ReadFile, WriteFile, CloseHandle, Sleep).

## 中文

内核只暴露一套内部服务 ABI（`bsd45_syscall()`，原生 4.5BSD 编号）。子系统
把外部 API 翻译成这套 ABI，不直接碰内核内部结构：

- `subsys.c` —— 注册表、`set_personality`/`get_personality`（424/425）以及
  到当前 personality 的分发。
- `subsys_unix.c` —— 原生 4.5BSD ABI（默认）。
- `subsys_posix.c` —— POSIX 表面，显式过滤非 POSIX 调用。
- `subsys_linux.c` —— Linux i386 编号到原生编号的翻译（`mkdir 39→136`、
  `wait4 114→7`、`mmap2 192→197`、`exit_group 252→exit`）。
- `subsys_win32.c` —— 最小 Win32 句柄 API（GetCurrentProcessId、
  CreateFileA、ReadFile、WriteFile、CloseHandle、Sleep）。
