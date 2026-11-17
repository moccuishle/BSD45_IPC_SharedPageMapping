# 手动测试：用户态多线程抢占模型（KLT）

本内核的线程是**内核态对象**（`struct kthread`），TCB、内核栈、调度、run queue
全在内核；用户态的 `pthread_*` 只是 syscall 包装。下面给你三套手动测试，
从"看现象"到"看内部状态"。

## 准备工作：构建带测试程序的 ISO

```powershell
cd C:\Users\Administrator\Desktop\work\bsd45
$env:HTTPS_PROXY=""; $env:https_proxy=""; $env:HTTP_PROXY=""; $env:http_proxy=""
powershell -NoProfile -ExecutionPolicy Bypass -File .\build_all.ps1
```
（Ubuntu 上用 `bash build_all.sh`）

ISO 里已放好：`PTHREAD.ELF`（创建/join/detach）、`PTHPRE.ELF`（抢占交错）、
`SCHEDTES.ELF`（优先级+继承自测）。注意 ISO9660 是 8.3 文件名，
`pthpreempt` 在盘上叫 `PTHPRE.ELF`，`schedtest` 叫 `SCHEDTES.ELF`。

## 启动

```powershell
& "C:\msys64\ucrt64\bin\qemu-system-i386.exe" `
  -cdrom out\BSD45-ELF-x86.iso -boot d -m 64 -serial stdio
```
（VMware：打开 `vms\BSD45-install.vmx`，或 `BSD45-diskboot.vmx` 从已装好的盘启动）
启动后先点一下窗口捕获键盘，出现 `bsd45>` 提示符即可开始。

---

## 测试 1：多线程抢占交错（最直观）

在 monitor 里：
```
bsd45> shell
# /bin/pthpre
```

**看什么**：两个线程在同一进程里各打印 12 个字母。`A`/`B` 交替出现
（如 `ABAB...`），而不是 `AAAA...BBBB`。

**为什么说明抢占**：两个线程都不主动让出（只 `write` 一个字母），
唯一的切换来源是 PIT 时钟中断。字母交错 = 时钟在 ring3 抢占了当前线程，
换到另一个 TCB。最后 `pthpreempt: PASS`。

## 测试 2：pthread 完整生命周期

```
# /bin/pthread
```

**看什么**：
```
pthreadtest: created 4 threads, self=<tid>
pthreadtest: counter=4000 join=ok      <- 4 线程各跑 1000 次累加
pthreadtest: detach counter=5000       <- detach 线程无人 join 也能跑完
pthreadtest: PASS
```
证明：内核能创建多 TCB、各自调度、join 回收、detach 自回收。

## 测试 3：内核侧调度状态与优先级/继承自测

回到 monitor（在 shell 里 `exit 0`）：
```
bsd45> kthread
bsd45> schedtest
bsd45> clock
```

**看什么**：
- `kthread:` 列出所有 TCB（tid/类型 user|kern/状态/内核栈 esp），
  `switches` 是切换次数——多线程运行时它会增长
- `schedtest: 20-level prio + inheritance + preempt-gate PASS`
  ——验证 20 级优先级（0 最低/19 最高）、高抢低、优先级继承与恢复、
  以及禁抢占门（用户态可抢占、内核临界区不可抢占）
- `clock: ticks ... quantum 2` ——时钟节拍与时间片

## 测试 4：观察优先级 API（在 shell 里）

```
# /bin/schedtes
```
`schedtest: setprio ok` 证明 syscall 446/447（sched_setprio/getprio）工作；
20 级优先级的动态提升/衰减/继承由测试 3 的内核自测覆盖。

---

## 进阶：动态设优先级看调度倾斜

想亲手验证"高优先级抢占低优先级"，可在代码里改：
- `libexec/pthpreempt.c`：给 worker_a 加 `sched_setprio(18)`、worker_b 加
  `sched_setprio(2)`，重建后跑 `/bin/pthpre`。高优先级线程应更频繁被选中，
  输出里 A 明显多于 B（优先级影响"选中顺序"，不改变固定时间片长度）。

## 判断标准汇总

| 现象 | 说明 KLT 正常 |
|---|---|
| A/B 交错 | PIT 在 ring3 抢占、切 TCB |
| 4 线程 counter=4000 | 内核创建/调度/回收多 TCB |
| kthread 列出 user 线程 + 各自内核栈 esp | 每线程独立内核栈 |
| switches 增长 | 真正发生了线程切换 |
| schedtest PASS | 优先级/继承/禁抢占语义正确 |

## 常见问题

- 命令找不到：ISO 用 8.3 名，`pthpreempt`→`PTHPRE.ELF`、`schedtest`→`SCHEDTES.ELF`
- 输出没有交错：确认不是只跑单线程；`pthpre` 必须创建两个线程
- 在 VMware 里敲键盘没反应：先点一下 VM 窗口捕获输入
