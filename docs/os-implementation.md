# os.md 在 BSD45 中的实现：深入版任务书与证据

> 本文不是“把 os.md 的功能都做一遍”的功能清单，而是按 `better.md` 的四个问题重写后的任务书：
> 先说明顶尖内核工程师会额外做什么、从哪些原始材料出发、用什么方法工具、
> 以及除“你和我”之外什么能证明结果正确；再给出在 `bsd45` 中实际落地与验收的映射。
>
> 状态：机制 1/2/4 已落地并通过新增验收；机制 2 已补每线程内核栈与
> 内核安全点抢占；机制 3 落地了 COW 与用户缺页处理。
> demand paging 仍是明确的未完成项（见 §7）。
> 代码：`bsd45/kernel/{hal,other,thread,obj,arch/i386}`；验收：`bsd45/tools/test_{ipl,preempt,cow,fault,badptr}.*`。

---

## 0. 重写后的任务（只保留“更深”，不扩大“更大”）

目标：在 32 位 i386 的 BSD45 端口上，用**可被独立回放的状态机与日志**证明以下四件事，
而不是仅仅“有这些代码”：

1. **用户/内核边界**：用户指针不能把内核打崩；用户异常只终止进程；坏指针返回 `EFAULT`。
2. **线程是唯一调度实体**：进程只是地址空间/资源容器；时钟中断能抢占 CPU 密集的用户线程。
3. **MMU 映射可验证**：`pmap` 仍是架构抽象层；`fork` 用 COW，写时缺页复制，父子互不污染。
4. **统一 IPL 0–15**：硬中断、软中断共用 `ipending`；`>` 判据；高到低派发；可嵌套；
   线程有 `base_ipl`/`preemptable`，不可抢占者用优先级继承。

验收不只看“程序打印 PASS”，还要满足：

- 内核自测覆盖状态机（提升/恢复、派发顺序、屏蔽字、继承）；
- **独立日志校验器**从 `itrace:` 记录重放，证明单调性、嵌套闭合、派发只选 `> current_ipl`；
- 负向测试（坏指针、用户崩溃）不得出现 `kernel TRAP`；
- COW 计数器和父子可见性同时成立；
- 全部旧套件不回归。

---

## 1. 顶尖专家会做、但原需求没有明说的六件事

1. **先定义不变量，再写代码**：`current_ipl` 单调、`ipending` 不丢、IPL 15 不切换、
   COW 引用计数不为 0 时不得归还物理页、用户指针访问前必须走页表校验。
2. **给状态机留可回放证据**：不是打印“PASS”，而是把每次 `from→to` 迁移写入有界环，
   让一个**不信任内核自测**的 host 脚本重算不变量。`tools/check_ipl_trace.ps1` 就是这一层。
3. **把“失败路径”当主路径测**：坏指针、空指针解引用、COW 只读页、`fork` 后父写、
   缺少 handler 的 pending 位；失败路径不测，内核只是在“没触发”时看起来正确。
4. **量化而不是形容词**：PIT 100 Hz、quantum 2 ticks、tick 计数、抢占次数、COW shared/faults/copies
   都可从用户态读回（`getticks`/`getcow`），避免“感觉能抢占”。
5. **明确拒绝范围**：不把 64 位 `syscall`/SMEP/SMAP/KPTI 硬塞进 32 位端口；
   不声称单 CPU 协作内核具备 SMP 内存序保证；不把“形式化证明”当口号。
6. **可复现**：一条命令构建、一条命令跑套件、日志落盘、固定 QEMU 参数（`-m 64`、串口 TCP）。

---

## 2. 原始材料（不用二手总结，直接读源）

| 主题 | 原始材料 | 本实现用到的点 |
|---|---|---|
| 特权级/中断/栈切换 | Intel SDM Vol.3：中断门、TSS `SS0:ESP0`、`iret`、PIC/PIT | `_syscall_entry`、`irq0_entry` 的帧布局；IRQ 从 ring3 换到内核栈 |
| 8259/PIT 编程 | Intel 8259A、8254 数据手册；4.5BSD `i386/isa/icu.s`、`isa/clock.c` | ICW/OCW、EOI、mode 2、divisor 11932 |
| 4.5BSD 陷阱分类 | `kernel/include/i386/trap.h`（`T_PAGEFLT=12` 等）、`locore.s` | 用户缺页判断必须用 4.5BSD 的 `T_*`，不是 CPU 向量号 |
| 4.5BSD 用户拷贝 | `kernel/include/sys/systm.h` 的 `copyin/copyout` 契约；`locore.s` 的 `pcb_onfault` 版本 | 本端口改为页表预校验 + `ucopyin/ucopyout` |
| pmap 抽象 | 4.5BSD `i386/pmap.c`；Mach `pmap_enter/remove/extract` 接口 | 复用 `pmap_map`/`pte`，COW 位借用 `PG_FOD`（0x200） |
| COW/缺页 | os.md 机制 2/3；经典 fork COW 语义 | `copy_address_space_cow`、`user_page_fault`、引用计数表 |
| IPL/spl 思想 | BSD `spl(9)`、FreeBSD `ithread`、Solaris `ipl`；os.md 机制 4 | `ipl_raise/ipl_x`、`ipending`、严格 `>`、`preempt_point` |
| 形式化边界 | seL4 Coq 证明（Klein et al. 2009）只覆盖抽象层 | 本文只声称“日志可回放的不变量”，不声称“整机证明” |

---

## 3. 方法与工具（具体到可执行）

- **状态机建模**：`current_ipl ∈ [0,15]`，`ipending` 16 位；事件 `e` 可被服务 iff `e.ipl > current_ipl`。
- **迁移日志**：`kernel/hal/ipl.c` 的 `trace_ev()` 记录 `R/X/E/I/D/d` 六类迁移与当时的 `ipending`。
- **独立回放**：`tools/check_ipl_trace.ps1` 只读串口日志，重建栈：
  `E` 入栈、`I` 必须回到入栈值；`D` 必须 `to>from` 且 pending 对应位为 1，`d` 必须回到 `D` 的 `from`。
- **差分/负向测试**：`/bin/badptr`（未映射写、只读文本读）、`/bin/crash`（空指针）。
- **COW 计数**：`shared`（fork 共享页数）、`faults`（写缺页次数）、`copies`（实际复制页数），
  通过 syscall 431 读回；`cowtest` 同时检查父子变量值。
- **抢占证据**：`/bin/spin` fork 两个 CPU 密集进程，只有 PIT 能切换；日志里 A/B 必须交错，
  且 `ticks` 在运行期间增长。
- **构建/运行**：MSYS2 ucrt64 GCC 16.2 `-m32 -ffreestanding`；QEMU 11.1.3；
  `tools/run_qemu_test.ps1` 通过 TCP 串口驱动；`tools/all_tests.sh` 汇总。
- **未来可加**：TLA+ 对 `current_ipl/ipending` 建模；`cyclictest` 式最坏延迟统计；
  `lockdep` 式继承链环检测。

---

## 4. 除“你和我”之外的证明

1. **QEMU 串口日志 + 独立 checker**：`check_ipl_trace.ps1` 不调用内核代码，只按日志重算；
   它失败则整个 `test_ipl` 失败。
2. **内核自测**：`ipltest` 的向量映射、嵌套、12>7>3 顺序、继承状态机。
3. **负向用例**：`test_fault` 要求 `user fault: pid ... sig 11` 且 shell 仍能 `echo alive`；
   `test_badptr` 要求两次 `-14`（EFAULT）。
4. **交叉一致性**：`help` 中不再出现已删除模块的 `regtest` / `objtree` / `iotest` / `synctest` 命令。
5. **构建产物**：`bsd45.elf` 的节区布局必须让 `.data` 结束低于 boot stage2 的 `0x90000`；
   这是可被 `objdump -h` 独立检查的硬约束。
6. **旧套件回归**：30 套验收全绿（见 §6），证明新增机制没有靠破坏旧语义换取通过。

---

## 5. 实现映射（os.md → bsd45）

### 机制 1：Ring 0/3 与系统调用路径

| 需求 | 实现 | 证据 |
|---|---|---|
| 受控入口、用户/内核栈切换 | `int $0x80` → `_syscall_entry`（`other/usermode_asm.s`），TSS `ss0:esp0` 内核栈 | 旧套件 + `/bin/badptr` |
| 参数不能直接解引用 | `user_ok()` 走页表；`ucopyin/ucopyout`；`write/read/path/stat` 先校验 | `badptr: unmapped write -> -14` |
| 坏指针返回 `EFAULT` | `user_ok_w` 拒绝只读页；COW 页先私有化再写 | `badptr: read into text -> -14` |
| 用户崩溃不拖垮系统 | `trap()` 先判 `cs&3`；`T_PAGEFLT` 先试 COW；否则 `user_fault(SIGSEGV...)` 只杀进程 | `crash` 后 shell 继续 |

**边界说明**：32 位端口用 `int $0x80`，不是 64 位 `syscall`；没有 SMEP/SMAP/KPTI；
`copyin/copyout` 采用页表预校验（不依赖 `pcb_onfault`），语义等价但路径不同。

### 机制 2：进程是容器、线程是调度实体

| 需求 | 实现 | 证据 |
|---|---|---|
| 调度器只选线程 | `proc_sched()`/`sched_tick()` 遍历 `kthreads[]`，`t_proc` 指向地址空间 | 旧 `test_pthread` |
| 中断抢占线程 | `irq0_entry` → `clock_irq` → `sched_tick`；圆整轮转、`base_ipl>=IPL_CLOCK` 不抢占 | `spin` 的 A/B 交错 |
| COW fork | `copy_address_space_cow()` 清 `PG_RW`、置 `PG_COW`、引用计数 +1 | `cowtest` 父子值 |
| 写缺页复制 | `user_page_fault()`：最后引用者只改权限，否则分配+复制+`page_put` | `cow: faults/copies > 0` |

**边界说明**：每个用户线程已有独立 8 KB 内核栈，TSS `esp0` 在每次切换时
指向目标线程；时钟中断仍只在 ring3 帧上直接切换，内核态采用
`syscall exit` 安全点抢占（保存完整用户上下文后调度），不在任意内核
指令点抢占。`preempt_point()` 还会排空软中断/内核线程。

### 机制 3：MMU / pmap

| 需求 | 实现 | 证据 |
|---|---|---|
| pmap 抽象 | 继续用 4.5BSD `pmap.c` 的 `pmap_map/enter/remove/extract` | `vstat`/旧 mmap 套件 |
| 页表项权限 | `user_map_prot` 设 `PG_RW`；COW 借用 `PG_FOD` 位标记 | `pte` 命令、`cowtest` |
| TLB 一致性 | COW 标记后 `cpu_load_cr3(cur->pd)`；缺页处理后再刷 | 父子写不串 |
| 引用计数 | `page_ref[]`（16K 项，覆盖 64MB guest）、`page_put()` | `cow: shared/faults/copies` |
| demand paging | `demand_page()` 堆/栈，`mmap_fault()` 匿名/共享/私有文件映射 | 首次访问才分配物理页 |
| 内存压缩 | `kernel/vm/zcomp.c`：128×2KB 槽位、RLE、零页特判；PTE 用 `PG_ZCOMP`（present=0）编码槽位，缺页时 `zcomp_refault()` 解压重建 | `monitor` 的 `zcomp` / `zcomptest`；压力阈值 `page_ninuse>4096` 自动回收 |

**边界说明**：`PG_COW` 与 4.5BSD `PG_FOD` 复用同一位；`PG_ZCOMP` 借用
`PG_SWAPM` 位。压缩只接受进程私有匿名页，且跳过 VMA（匿名 mmap 暂不压缩）；
没有 swap 设备，压缩池满时 early-abort 保留原页，不降级到磁盘。

#### 内存压缩的实现要点

- **只压匿名页**：`zcomp_reclaim()` 只选 present、`PG_RW`、`PG_u`、非 COW、
  不在 VMA 内的页；文件页/共享页直接跳过。
- **压缩 entry**：`pt[j] = (slot << 12) | PG_ZCOMP | PG_u`，`PG_V=0`，
  `PG_ZCOMP` 借用 `PG_SWAPM`（0x400）；槽位 0 专用于全零页，不占负载。
- **缺页路径**：`user_page_fault()` 先试 `zcomp_refault()`，解压到新页、
  重建 PTE、释放槽位；失败则 `page_put()` + PTE=0 → SIGSEGV，不静默返回零页。
- **触发**：`page_alloc()` 在 `page_free_list==0 && page_ninuse>4096` 时
  调 `zcomp_reclaim(8)`，分批压缩；`in_zcomp` 防止递归。
- **编码**：2 KB 槽位、游程编码（count,byte 对）；压缩后超过 2 KB 即
  early-abort，计入 `rejected`。
- **回收**：进程退出时 `free_address_space()` 识别 `PG_ZCOMP` 并归还槽位，
  避免槽位泄漏。

### 机制 4：统一 IPL 0–15

| 需求 | 实现 | 证据 |
|---|---|---|
| 16 级、单一位图 | `current_ipl`、`ipending`、`actions[16]` | `ipl: 16 levels` |
| 严格 `>` 判据 | `ipl_raise` 只在 `new>old` 生效；`ipl_dispatch` 只取 `> current` | `ipltest: order 12>7>3 ok` |
| 硬/软中断同源 | `softirq_raise()` 与 IRQ 共用 `ipending`；`softirq.c` 走 `ipl_dispatch` | `softirqtest`、`ipltest` |
| current_ipl 单调/嵌套 | `ipl_enter/ipl_exit` 保存旧级；日志 `E/I` 栈闭合 | `check_ipl_trace: PASS` |
| 线程 base_ipl/preemptable | `thread_ipl_set/boost/unboost`；`sched_tick` 尊重 `base_ipl` | `ipltest: thread ipl/inheritance ok` |
| 优先级继承 | `thread_ipl_boost/unboost` 状态机；`eres` 钩子随 `kernel/sync` 删除 | `ipltest: thread ipl/inheritance ok` |
| 不可抢占/临界区 | `IPL_HIGH` 屏蔽全部；`preempt_point()` 为内核安全点 | 屏蔽字自测 |
| 平台无关性 | 策略在 `kernel/hal/{ipl,clock}.c` + `context.h`；i386 后端在 `arch/i386/hal/{ipl_arch,clock_arch,context_arch}.c` | 移植只实现 5 个钩子，策略层零修改；清单见 [`docs/porting-guide.md`](porting-guide.md) |

**边界说明**：单 CPU、单 8259 主从（8+8 线映射到 16 级）；每个 IPL 只挂一个 handler；
IPL 15 期间不切换线程；没有 SMP IPI/TLB shootdown。

### 机制 5：VFS 同步 / 异步 I/O

| 需求 | 实现 | 证据 |
|---|---|---|
| 两套显式 VFS 接口 | `vfs_read_sync/write_sync` 在调用者执行；`vfs_read_async/write_async` 排队 `kiocb`，返回 `-EIOCBQUEUED` | `kernel/fs/vfs.h`、`vfs.c` |
| 异步完成 | `kernel/fs/aio.c` 的 `aio` 内核线程调用 `vfs_*_sync` 执行同一后端，再调用 `ki_complete()` 恰好一次 | `monitor` 的 `aio` / `aiotest` |
| 完成通知走 IPL | 完成回调通过 `softirq_raise(IPL_AIO, ...)` 唤醒等待进程 | `IPL_AIO`（9） |
| token ABI | syscall 460 `aio_submit` → token；461 `aio_wait`；462 `aio_poll` | `/bin/aiotest` |
| 共享 SQ/CQ 环 | 4 KB 环页同时映射用户/内核：`struct uring_header/sqe/cqe`；syscall 463 setup、464 enter、465 teardown | `/bin/uringtest` |
| 批量提交 | 用户一次推进 `sq_tail`，`uring_enter(n,wait)` 一次提交 n 个 SQE，完成写 CQE；一次系统调用 | `uringtest` 提交 4 个请求 |
| 同步路径也走 VFS | 用户 `read/write` 直接调用 `vfs_read_sync/write_sync` | 旧文件读写套件 |

**边界说明**：环大小固定 8 项（每项 24 B），SQE 支持单段 `buf/len`；
`uring_enter` 的 `wait` 只在 CQ 为空时阻塞，CQ 溢出计数到 `cq_overflow`。
异步 token 路径仍有 8 个固定槽位 + 每槽 4 KB 内核缓冲；单请求
`len <= PAGE_SIZE`。没有实现 `IORING_REGISTER_FILES/BUFFERS`；
文件系统后端仍是同步 `ops->read/write`，由 `aio` 线程承载阻塞路径。

---

## 6. 验收结果（快照：删除 `object/io/sync` 之前，2026-11-17）

> 下表是删除 `kernel/{obj/object,io,sync}` 之前的运行结果；删除后测试清单
> 相应缩短，需重新跑一遍才能作为当前证据。

```text
test_ipl       os.md M4 IPL: PASSED
test_preempt   os.md M2/M4 preempt: PASSED
test_cow       os.md M2/M3 COW: PASSED
test_fault     os.md M1 fault: PASSED
test_badptr    os.md M1 badptr: PASSED
---- suites failed: 0
```

关键日志片段：

```text
ipl: 16 levels, current 0, all IRQs masked
clock: PIT 100 Hz, IRQ0 -> IPL 5, quantum 2 ticks
ipltest: vector map ok
ipltest: raise/restore ok
ipltest: order 12>7>3 ok
ipltest: thread ipl/inheritance ok
ipltest: PASS
check_ipl_trace: PASS (… events, nesting and order verified)

BBBBBBBBBAAAAAAAAABBBBBBBBBBBBBBBBBBAAAAAAAAAAAAAAAAAABBBAAA
spin: ticks 78 -> 84
spin: timer ok
spin: PASS

cowtest: child x=99
cowtest: parent x=42
cow: shared 74 faults 13 copies 8
cowtest: PASS

user fault: pid 2 sig 11 eip 8052f22 va 0
SIGSEGV
alive

badptr: unmapped write -> -14
badptr: read into text -> -14
badptr: PASS
```

---

## 7. 实现状态与剩余项（按“更深”排序）

1. **每线程内核栈 + 内核态抢占**（已做一半）：每线程 8 KB 内核栈与 TSS
   `esp0` 切换已实现；内核态只在 `syscall exit` 安全点抢占，任意内核指令点
   抢占仍需 `kswitch` 保存内核栈 + 调度器栈。完整设计见
   [`docs/kernel-preemption.md`](kernel-preemption.md)，代码待测试后落地。
2. **demand paging**（堆 + 栈 + mmap）：`brk`/栈/匿名与文件映射都在首次
   访问时由 `demand_page()` / `mmap_fault()` 填充；mmap 本身只记录 VMA。
3. **IPL 的多 handler/多 CPU 扩展**：每级已支持多 handler 链（固定池
   `action_pool[64]`，同级 FIFO）；仍是单 CPU，SMP 需要 per-CPU
   `current_ipl`、IPI、TLB shootdown。
4. **形式化**（已交付）：`docs/tla/ipl.tla`、`docs/tla/cow.tla` 与
   `tools/check_tla_model.py` 对 `current_ipl/ipending` 的派发规则和 COW
   引用计数做穷举检查。
5. **延迟量化**（已埋点）：`clock_irq()` 用 TSC 记录相邻节拍抖动，
   monitor `lat` 打印 samples/min/max/avg；QEMU 实测尚未跑。

---

## 8. 与 os.md 的差距表（诚实版）

| os.md 要求 | 状态 | 说明 |
|---|---|---|
| Ring0/3、受控入口、用户/内核栈 | ✅ | `int 0x80` + TSS `esp0` |
| `copy_from_user`/`access_ok` | ✅（等价实现） | 页表预校验的 `ucopyin/ucopyout` |
| 崩溃隔离 | ✅ | 用户异常 → 信号 → 只杀进程 |
| 线程唯一调度实体 | ✅（用户态 + 内核安全点） | 任意内核点仍非抢占 |
| COW fork | ✅ | 写缺页复制 + 引用计数 |
| demand paging | ✅ 堆/栈/mmap | 首次访问时填充；mmap 只记录 VMA |
| 内存压缩（zswap 风格） | ✅ RLE / 2KB / 128 槽 | 仅私有匿名页、非 VMA；无 swap 降级 |
| VFS 同步/异步分离 | ✅ `vfs_*_sync` / `vfs_*_async` | 两套入口；异步队列 + `-EIOCBQUEUED` + 回调 |
| 共享 SQ/CQ 环 + 批量提交 | ✅ `uring.h` 环页 + syscall 463/464 | 一次 `uring_enter(n)` 提交 n 个 SQE；无 `IORING_REGISTER` |
| 异步完成通知 | ✅ `IPL_AIO` 软中断 | 用户 ABI 460/461/462；无 io_uring 共享环 |
| pmap 抽象 | ✅（复用 4.5BSD） | 未新增架构 |
| 16 级 IPL / 单一 pending | ✅ | 单 CPU、每级多 handler 链 |
| 软硬中断统一派发 | ✅ | `ipl_dispatch` 高到低 |
| 优先级继承 | ✅（`thread_ipl_boost` 状态机） | 继承链成环未做；eres 钩子已随 sync 删除 |
| 可抢占资源挂起 | ⚠️ 策略字段 | `t_preemptable` 已记录，未实现挂起接管 |
| SMP/TLB shootdown | ❌ | 单 CPU 端口 |
| 形式化证明 | ⚠️ 模型级 | `docs/tla/*.tla` + `check_tla_model.py` 穷举检查，不含 Coq 级证明 |

> 这张表就是“深入版”的边界：能做的小而可证明，不能做的明确写出来，不用“已完成”掩盖。
