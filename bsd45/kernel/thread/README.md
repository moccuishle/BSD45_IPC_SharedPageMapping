<!-- Copyright (c) 2026 jialiang.  All rights reserved. -->

# `kernel/thread` — kernel threads and softirq carriers

## English

This directory provides execution carriers for work that must not run on a
random user thread's kernel stack:

- `kthread.c` / `kthread.h` — a small pool of kernel threads with their own
  8 KB stacks, cooperative block/wake/exit and `kthread_spawn/run/poll`.
- `kswitch.s` — saves the callee-saved registers and switches kernel stacks.
- `softirq.c` — pending-bit soft interrupts (`SOFT_RECLAIM`, `SOFT_INPUT`,
  `SOFT_NET`, `SOFT_TEST`) executed by the `softirq` kernel thread; the page
  cache asks it to reclaim pages when it runs out.

User threads will share the same scheduler model in a later stage.

## 中文

本目录为“不能借用任意用户线程内核栈”的工作提供执行载体：

- `kthread.c` / `kthread.h` —— 内核线程池，每个线程有独立 8 KB 栈，支持
  协作式 block/wake/exit 以及 `kthread_spawn/run/poll`。
- `kswitch.s` —— 保存 callee-saved 寄存器并切换内核栈。
- `softirq.c` —— 待处理位图软中断（`SOFT_RECLAIM`、`SOFT_INPUT`、
  `SOFT_NET`、`SOFT_TEST`），由 `softirq` 内核线程执行；页缓存耗尽时会
  请求它回收页面。

后续阶段的用户线程会复用同一套调度模型。
