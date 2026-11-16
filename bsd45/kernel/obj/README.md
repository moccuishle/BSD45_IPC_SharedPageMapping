<!-- Copyright (c) 2026 jialiang.  All rights reserved. -->

# `kernel/obj` — kernel strings

## English

This directory keeps the kernel string implementation:

- `kstr.c` / `kstr.h` — kernel strings; `KERNEL_UNICODE=1` builds UTF-16LE
  with both A (UTF-8 boundary) and W (native UTF-16) APIs, `KERNEL_UNICODE=0`
  builds ASCII with the A API only.

The object manager (`object.c` / `object.h`) has been removed together with
its namespace, handle table and object self test.

## 中文

本目录只保留内核字符串实现：

- `kstr.c` / `kstr.h` —— 内核字符串；`KERNEL_UNICODE=1` 构建 UTF-16LE，
  同时提供 A（UTF-8 边界）和 W（原生 UTF-16）两套 API；`KERNEL_UNICODE=0`
  构建 ASCII，只保留 A 套 API。

对象管理器（`object.c` / `object.h`）已连同命名空间、句柄表和对象自测一起删除。
