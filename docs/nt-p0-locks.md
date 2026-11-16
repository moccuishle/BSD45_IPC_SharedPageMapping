# NT P0 同步原语 → BSD45 对象内核：优先级与改动清单

> 结论：**先做 P0 同步原语，再做线程，再做 I/O/文件系统。**
> 原因：线程、IRP 完成端口、页回收、命名空间都会共享内核数据结构，先把
> “排队自旋锁 + 可睡眠性断言 + push lock + ERESOURCE 属主捐赠”落地，后面
> 每加一个并发结构都不需要返工。
>
> 说明：`NT-BSD45-handoff.md` 的“不要搬对象管理器/注册表/IRP”是**上一版**
> 的结论；用户后来明确要求这些对象化/IRP/IOCP 特性都要做。本文件保留
> handoff 的 P0 机制建议，同时把对象化列为后续阶段，两者不冲突：
> P0 是并发基础，对象化是资源模型。

---

## 1. 已落地（本轮）

| 项 | 文件 | 状态 |
|---|---|---|
| 排队自旋锁（MCS） | `kernel/sync/sync.c`：`qspin_lock/trylock/unlock` | ✅ UP 原型（`synctest`） |
| push lock | `kernel/sync/sync.c`：共享/独占单字锁 | ✅ 原型 |
| ERESOURCE 属主跟踪 + 优先级捐赠 | `kernel/sync/sync.c`：`eres_acquire/release`，等待者优先级高于属主时提升并记账 | ✅ 原型 |
| 可睡眠性断言 | `kernel/sync/sync.h`：`KASSERT/PAGED_CODE/ASSERT_SLEEPABLE`，检查 `cpl` | ✅ 宏 + 自测 |
| 对象命名空间加锁 | `kernel/obj/object.c`：`obj_lookup_u()` 持 `obj_lock` | ✅ |
| 硬件收敛到 HAL | `kernel/hal/hal.c`：ATAPI/pccon 只调 `hal_inb/outb/insw/outsw/outw` | ✅ 22 套全绿 |
| 验收 | `tools/test_sync` + `tools/test_obj` 等 23 套 | ✅ |

当前 `qspin` 的原子操作是 **UP 版**（内核关中断、单 CPU 下用普通读写），
SMP 版需要 `lock xchg/lock cmpxchg`；由于 `-march=i386` 不生成 `cmpxchg`，
SMP 补丁要用汇编或 `-march=i486`，见第 3 节。

---

## 2. 优先级（我的决定）

| 顺序 | 事项 | 为什么排这里 | 验收 |
|---|---|---|---|
| **1** | **P0 同步原语**（已完成原型，继续接入 VFS/IRP/线程） | 后面所有并发结构的地基；handoff 要求必做 | `test_sync` + 全绿 |
| **2** | **真线程**：thread 对象挂 process、调度器只认线程、`thread_create/exit/join`、线程定向信号、fork 只复制调用线程 | 用户明确的第一步；IOCP 需要线程阻塞/唤醒语义 | `test_thread` |
| **3** | **IOCP 用户 ABI**：`aio_submit`/`io_port_get`，close 时 rundown/取消未完成 I/O | 依赖线程 + IRP；handoff 风险 2 | `test_iocp_user` |
| **4** | **VFS 字符串迁移到 `kstr`**（A/W 边界只留 syscall） | 文件系统、ZFS/CNFS 都要求 UTF-16 名字，必须先统一 | ASCII/UTF-16 双构建全绿 |
| **5** | **FS 框架 + FAT32 只读→读写** | 第一个真实磁盘文件系统，验证 bdev/HAL/缓存 | `test_fat` |
| **6** | **exFAT 只读→读写** | 与 FAT32 共用簇/目录基础设施 | `test_exfat` |
| **7** | **NTFS 只读**（MFT/属性/runlist/目录 B+ 树） | 只读可先落地，写入需要日志 | `test_ntfs` |
| **8** | **CNFS**（日志 + ACL + 多流 + 稀疏 + 配额） | 自研日志式 FS；依赖 4/5 的缓存与对象模型 | `test_cnfs` |
| **9** | **ZFS 只读→写、DTrace** | 高级模块；ZFS 依赖 bdev/校验和/事务组，DTrace 依赖线程/探针点 | `test_zfs_ro`、`test_dtrace` |
| **10** | **时钟中断 + 抢占 + 每线程内核栈** | 让线程从协作式变抢占式；需要 ICU 接线（当前全程关中断） | 抢占测试 |

---

## 3. P0 逐文件改动清单（下一 patch）

### 3.1 排队自旋锁替换 `simple_lock`

| 文件 | 当前 | 改动 |
|---|---|---|
| `kernel/include/machine/param.h` | `struct simplelock`（`NCPUS==1` 时宏为空） | 换成 `struct qspin` 或保留名字、内部改为 qspin 字段 |
| `kernel/include/sys/lock.h` | `simple_lock/simple_unlock/simple_lock_try` 宏 | 映射到 `qspin_*`；DEBUG 版本保留文件/行号 |
| `kernel/sys/kern_lock.c` | `lockmgr` 里用 `lk_interlock` 的 `simple_lock` | 保留 lockmgr 语义，只替换 interlock 实现 |
| `kernel/arch/i386/isa/icu.s` | `spl*` 与 `_cpl` | `qspin` 的 SMP 版需要 `lock` 指令；UP 版沿用当前关中断 |
| SMP 版本 | 无 | `qspin_lock/unlock` 改用 `lock xchg` / `lock cmpxchg`（汇编内联或 `-march=i486`），每 CPU 一个 `qnode` |

### 3.2 可睡眠性断言（KASSERT/PAGED_CODE）

| 文件/函数 | 断言 |
|---|---|
| `vm/vm_fault.c:vm_fault()` | 入口 `ASSERT_SLEEPABLE()`；持页锁后 `KASSERT(cpl != 0, ...)` 视路径 |
| `vm/vm_page.c` 页分配/回收 | 分配可睡眠、回收不可在中断上下文 |
| `sys/vm_pageout.c` / `bufdaemon` | 守护线程入口 `ASSERT_SLEEPABLE()` |
| `kern/kern_synch.c:sleep/wakeup` | `sleep` 前 `KASSERT(cpl == 0)`，`wakeup` 可在任意 spl |
| `kern/kern_clock.c:schedcpu` | 软时钟上下文只允许 `splsoftclock` 级别 |
| 新代码（对象/IRP/线程） | 凡可能阻塞的入口统一 `ASSERT_SLEEPABLE()`；持 `qspin` 的路径 `KASSERT` 禁止睡眠 |

### 3.3 push lock 接入点

- fd 表（`struct uproc.fds`）的分配/关闭/复制；
- name cache（`vfs_cache.c`）的查找/插入；
- mount 表读多写少；
- IRP 队列的提交/完成（与 `qspin` 二选一，短临界区用 push lock）。

### 3.4 ERESOURCE 属主捐赠接入点

- VFS `vnode` 锁（`lockmgr` 的 `lk_lockholder` 已有 pid，可挂 owner）；
- 文件系统日志事务（CNFS/ZFS 写路径）；
- 只对**有明确属主**的锁做捐赠，限制提升上限（NT 上限 14），释放严格衰减。

### 3.5 验收

- `buildkernel` / `build_all.ps1` 通过；
- `tools/all_tests.sh` 23 套全绿；
- `synctest` 覆盖 lock/trylock/unlock、共享/独占、捐赠、PAGED_CODE；
- 后续接入 VFS/IRP 时每步保持全绿。

---

## 4. 明确不做（沿用 handoff）

- 不搬 NT 的 VAD/PTE/PFN（BSD 的 `vm_map/vm_object/pv_entry` 已是同一层抽象）；
- 不搬 NT 调度器（32 级 + boost 过时）；
- 不搬 SEH（用 `copyin/copyout` 的 EFAULT 语义）；
- 不一次性替换全部 `lockmgr`；第一 patch 只做锁，不改语义。
