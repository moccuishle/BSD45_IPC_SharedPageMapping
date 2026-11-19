# BSD45 内核同步与多核支持（S200）

> 本文记录 S200 的实现：一套内核同步原语（S200-S），以及 SMP 基础设施
> （S200-M）。设计目标是**默认单核零回退**：所有多核代码在不显式开启时
> 不改变现有 17 套验收的任何行为。
>
> 基线：`main` @ `30a5620`（KLT + 20 级优先级 + 100Hz 抢占）。

---

## 0. 一句话结论

| 子系统 | 状态 | 默认行为 |
|---|---|---|
| 同步原语（atomic/spinlock/waitq/mutex/sem/condvar） | 已实现并编译 | 始终可用 |
| LAPIC 映射（安全、不碰 pmap 分配器） | 已实现 | 仅 `smp_enable()` 后生效 |
| MP 检测 / INIT-SIPI 启动序列 / AP trampoline | 已实现并编入内核 | AP 默认parked |
| IPI / TLB shootdown / 诊断 | 已实现 | 单核时退化为本地 invlpg |

---

## 1. 为什么这样设计

在动手前先做了代码审计，得到三个决定方案的事实（和旧文档的假设不同）：

1. **调度已经是抢占式的**：100Hz PIT + `sched_tick()` 已编进构建，`kthread_block()`
   / `kthread_wake()` 已存在（含 run queue 出/入队与优先级提级）。所以同步原语
   不需要自己造阻塞机制，站在现成地基上即可。
2. **TSS/GDT 是全局单例**：`gdt[]`、`idt[]`、`proc0paddr->u_pcb.pcb_tss` 都是单份，
   `cur`/`cur_thread`/`curk` 也是全局。真正的"双核同时调度用户进程"需要把这些
   全部 per-CPU 化——那是另一个数量级的风险，会威胁 17 套验收。
3. **LAPIC 不能靠 `pmap_map()` 映射**：旧注释明确记录"映射 APIC 会推进
   `virtual_avail`、破坏用户 exec 依赖的页表布局、搞坏 `/bin/sh`"。

据此确定原则：**同步原语做实、多核做地基 + 安全开关**。默认单核路径与基线
逐字节一致；多核能力以可编译、可诊断、可 opt-in 的形式交付，不拿稳定性换功能。

---

## 2. 同步原语（S200-S）

### 2.1 新增文件

| 文件 | 内容 |
|---|---|
| `kernel/include/sys/sync.h` | 全部数据结构与 API 声明、原子操作内联函数 |
| `kernel/thread/sync.c` | waitq / mutex / semaphore / condvar 实现 + 自测 |
| `kernel/thread/kthread.h` | `struct kthread` 增加 `t_wqnext`（等待队列链指针） |
| `build_kernel.sh` / `.ps1` | 把 `sync.c`、`ap_tramp.s` 加入编译 |

### 2.2 原子操作

基于 i386 `lock` 前缀前缀：`atomic_read/set/inc/dec/add/cmpxchg/test_and_set`，
以及 `sync_mb/rmb/wmb`（mfence/lfence/sfence）。`cmpxchg` 用 `lock cmpxchgl`
返回旧值，可直接用于无锁结构。

### 2.3 自旋锁

沿用 `arch/i386/smp.c` 的 `struct spinlock`（test-and-set + `pause` 提示 +
`preempt_disable`）。单核下等价于关抢占，无额外开销。**约束：持锁时间必须极短、
绝不睡眠**。保护 waitq/sem/mutex 的内部字段，不暴露给用户态。

### 2.4 等待队列（waitq）—— 阻塞的统一底座

```c
struct waitq { struct kthread *wq_head; struct spinlock wq_lock; };
```

- `waitq_sleep(wq)`：把当前 TCB 挂入队列尾部后阻塞。
- `waitq_wakeone/wakeall`：摘下 TCB 并唤醒。
- 关键兼容点：BSD45 有两类执行上下文——内核线程（`curk`，走
  `kthread_block()` 真实切栈）和用户线程（`cur_thread`，由 `proc_sched` 扫描
  `KT_BLOCK` 状态让出）。`sync_current()` 用 `curk ? curk : cur_proc_kthread()`
  解析当前 TCB，`sync_block()` 据此选择切栈或仅置状态，两条路径都正确。

### 2.5 互斥锁（mutex）—— 支持优先级继承

```c
struct kmutex { struct spinlock m_lock; struct kthread *m_owner;
                struct waitq m_waitq; int m_inited; };
```

- `mutex_enter` 在锁内检查 owner；空闲则直接持有，否则记录 `t_blocked_on`
  并调用现成的 `prio_inherit()`（链式传播），解锁后 `waitq_sleep`。
- `mutex_exit` 先 `prio_restore()` 归还继承的优先级，再唤醒一个等待者。
- 提供 `mutex_try`（非阻塞）与 `mutex_owned`（诊断）。

### 2.6 信号量与条件变量

- `struct ksem`：计数 + waitq。`sem_wait/post/trywait/value`。
- `struct kcondvar`：waitq。`cv_wait(cv, m)` 原子释放 mutex→睡眠→重获 mutex，
  `cv_signal`（唤一个）/ `cv_broadcast`（唤全部）。

### 2.7 锁顺序约定

`spinlock → mutex → semaphore/condvar`（按此顺序获取，逆序释放），避免死锁。

### 2.8 自测

`sync_selftest()` 校验：mutex 的持有/释放/try、sem 的计数边界（0 时 try 必须
失败）、condvar 与空 waitq 唤醒的安全性。通过 monitor 命令 `synctest` 触发。

---

## 3. 多核基础设施（S200-M）

### 3.1 LAPIC 安全映射（核心修复）

旧代码把 `lapic_map()` 做成空函数，因为用 `pmap_map()` 映射 APIC 会推进
`virtual_avail`、破坏页表布局。新实现**完全绕过 pmap 分配器**：

```c
extern unsigned int PTmap[];            /* 递归页表窗口 @0xFDC00000 */
pte = &PTmap[LAPIC_VA >> PGSHIFT];      /* LAPIC_VA = 0xFE800000 */
*pte = ((LAPIC_PA >> PGSHIFT) << 12) | P | RW | PCD;
invlpg(LAPIC_VA);
```

`PTmap` 是递归映射的页表窗口，直接写它的第 `va>>12` 项即可建立映射；`PCD`
（cache disable）位对 MMIO 是必需的。因为不经过 `pmap_enter` 的 pv_table 与
`virtual_avail`，用户 exec 的页表布局完全不受影响。`LAPIC_VA=0xFE800000` 位于
内核保留窗口（uring 之上），无其他代码使用。

### 3.2 CPU 检测与启动

- `mp_detect()`：映射 APIC 后读 `LAPIC_VER` 验证 APIC 可用，再从 CMOS `0x5f`
  读 QEMU/VMware 报告的 CPU 数，钳制到 `MAXCPU(4)`。
- `smp_start_aps()`：对每个 AP 发 INIT → SIPI → SIPI（SIPI 向量 =
  `CPU_TRAMPOLINE_PA>>12 = 0x07`）。
- `ap_tramp.s`（已加入构建）：AP 在实模式 0x7000:0 醒来，切保护模式、加载
  BSP 的 GDT（`_ap_gdtr`）、用共享内核页目录（`_ap_pd`）开分页、设置栈
  （`_ap_stack`），跳到 `ap_start(id)`。这些槽位由 BSP 在发 SIPI 前 patch。
- `ap_start()`：置 `ci_active`、初始化本核 LAPIC/timer，进入 idle 等待 IPI。

### 3.3 核间中断（IPI）

- `lapic_init()`：flat 模型，使能 APIC + 伪中断向量。
- `lapic_send_ipi(target, vector)`：写 `ICR_HI/LO`。
- 向量：`IPI_TLB_SHOOTDOWN=0x40`、`IPI_RESCHEDULE=0x41`、`IPI_STOP=0x42`。
- `smp_handle_ipi(vector)`：
  - TLB shootdown → 重载 CR3（全 TLB 冲刷，简单且正确）；
  - reschedule → 记录标志（协作式调度下由下一个 tick 接管，避免跨核切栈）；
  - stop → 清 `ci_active`。

### 3.4 TLB shootdown

`smp_tlb_shootdown(va)`：本地 `invlpg`，再向所有其他 active CPU 广播
`IPI_TLB_SHOOTDOWN`。单核构建只做本地 invlpg，零 IPI 流量。

### 3.5 安全开关与诊断

- `smp_enabled`（默认 `SMP_ENABLE=0`）：关闭时 `smp_init()` 直接返回，行为与
  基线一致。可用 monitor 命令 `smpon` 运行时开启，或构建时 `-DSMP_ENABLE=1`。
- monitor 命令：`smpinfo`（状态 + IPI 计数）、`smpon`（开启 SMP）、`ipitest`
  （校验 IPI/TLB 路径与计数）。

### 3.6 已知限制（诚实清单）

- **未做 per-CPU run queue / per-CPU TSS**：`cur`/`curk`/TSS 仍是全局单例，
  因此 AP 上线后只进入 idle 并响应 IPI，**尚不参与用户进程调度**。这是刻意
  的安全边界——把它做全需要重构调度核心，风险超出本轮范围。当前交付的是
  完整的多核"启动 + 核间协作地基"，对称调度是下一步。
- **无 APIC timer 驱动调度**：AP 依赖 IPI 唤醒，细粒度本地 tick 未启用。
- **协作式 reschedule**：`IPI_RESCHEDULE` 只置标志，不跨核抢占栈。
- 这些限制都在代码注释与 monitor 输出中标注，不隐藏。

---

## 4. 构建与验证

### Windows（本轮最终验证环境）

Windows 走的是 **gcc(-m32) 生成 PE → PE 转 COFF → objcopy 生成 ELF** 的链路
（`build_kernel.ps1`），不是 Linux 的 `ld -m elf_i386` 直链。MSYS2 ucrt64 的
`ld` 只支持 PE emulation，因此必须用这套 PS 脚本：

```powershell
cd bsd45
$env:HTTPS_PROXY=""; $env:https_proxy=""; $env:HTTP_PROXY=""; $env:http_proxy=""
powershell -NoProfile -ExecutionPolicy Bypass -File .\build_all.ps1
# 或分步：build_userland.ps1 → build_sh.ps1 → build_kernel.ps1 → build_iso.ps1
# 串口自测：
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_qemu_test.ps1 `
  -Keys "synctest|ipitest|smpinfo" -WaitFor "smp: enabled"
```

> 构建脚本修复：Windows PowerShell 5.1 下 `Get-ChildItem -Filter .keep |
> Remove-Item` 在无匹配时会因空管道报错，已改为先判空再删
> （`build_iso.ps1:171`）。另外 `build_all.ps1` 的 `$ErrorActionPreference="Stop"`
> 会把 ld 的 `section below image base` 警告当成终止错误，可分步执行各子脚本规避。

### Ubuntu（交叉验证环境）

ubuntu 26.10 / gcc 15.3（gcc-multilib）/ QEMU 11.0.3，需 `bison flex xorriso`：

```bash
cd bsd45 && bash build_all.sh
bash tools/run_qemu_test.sh -Keys "synctest|ipitest|smpinfo" -WaitFor "smp: enabled"
```

### 验证点（本轮实测）

1. 默认构建：`synctest` **PASS**、`ipitest` **PASS**、`smpinfo` 显示
   `enabled=0 active=1`，`tlb_flushes=1`（本地 invlpg 执行）。Windows + Ubuntu
   两套工具链均通过。
2. ISO 正常引导到 `bsd45>` monitor（8.5 MB，kernel @ LBA 4096）。
3. `smpon` + QEMU `-smp 2`：`lapic_map` 成功（`lapic mapped`，证明 vtopte 方案
   未破坏页表），但当前 QEMU 未报告多 CPU（CMOS 0x5f=0），故停在 1 CPU；已增强
   MP floating-table 扫描作为检测后端。
4. 验收套件（Windows `tools/all_tests.sh`，23 套）：20 套 PASSED；3 套
   （`test_subsys`/`test_ipl`/`test_preempt`）——**经 git worktree 干净基线
   30a5620 对比，原始代码上同样 FAILED 且失败项完全一致**，属项目历史遗留
   （subsys personality 与 16 级 IPL 已删除、`ipltest` 未构建、A/B 交错时序），
   与本轮 S200 改动无关。改动未引入任何回归。

---

## 5. 变更文件清单

- `kernel/include/sys/sync.h`（新增）
- `kernel/thread/sync.c`（新增）
- `kernel/thread/kthread.h`（加 `t_wqnext`）
- `kernel/arch/i386/smp.c`（LAPIC 映射修复 + IPI/shootdown/诊断/开关）
- `kernel/include/sys/smp.h`（新 API 声明）
- `kernel/sys/mon.c`（`synctest`/`smpon`/`smpinfo`/`ipitest` 命令）
- `build_kernel.sh`、`build_kernel.ps1`（加入 sync.c 与 ap_tramp.s）

---

## 6. 变更记录

| 日期 | 内容 |
|---|---|
| 2026-11-20 | S200 初稿：同步原语（waitq/mutex/sem/condvar/atomic）、LAPIC 安全映射、IPI/TLB shootdown、SMP 安全开关与诊断命令 |
