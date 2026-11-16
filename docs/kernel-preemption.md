# 任意内核指令点抢占：设计（待测试后落地）

> 状态：**设计完成，未改运行路径**。
> 原因：这是中断入口 + 汇编上下文切换的改动，任何 off-by-one 都会 triple fault；
> 按当前“先不测试”的约束，先给出可审查的补丁设计，等允许 QEMU 验证后再写。
>
> 关联：`docs/os-implementation.md` §7.1；现有实现是“每线程内核栈 + syscall
> 出口安全点抢占”，本文补的是**任意内核指令点**被时钟打断后也能切换。

---

## 0. 平台无关分层（已落地）

抢占相关代码按“策略 / 架构后端”切开，移植新平台**不改策略层**：

```
kernel/hal/ipl.c           通用：IPL 0-15、ipending、handler 链、dispatch
kernel/hal/clock.c         通用：节拍、时间片、need_resched、延迟统计
kernel/hal/context.h       通用：5 个上下文钩子声明
kernel/thread/kthread.c    通用：线程表、内核栈分配、block/wake
kernel/arch/<arch>/hal/
    ipl_arch.c             hal_irq_to_ipl / hal_ipl_init / hal_ipl_apply
    clock_arch.c           hal_clock_init / hal_clock_eoi
    context_arch.c         hal_kernel_stack_set / hal_switch_stack /
                           hal_intr_from_user / hal_intr_user_resume
    (irq entry + kswitch)  该架构的汇编入口
```

移植新平台只需：

1. 把平台中断向量映射到 0–15：`hal_irq_to_ipl()`；
2. 实现中断控制器的 mask/EOI：`hal_ipl_apply()` / `hal_clock_eoi()`；
3. 实现定时器：`hal_clock_init()`，并让其中断进入通用 `clock_irq()`；
4. 实现上下文：`hal_kernel_stack_set()` / `hal_switch_stack()` /
   `hal_intr_from_user()` / `hal_intr_user_resume()`；
5. 提供该架构的 IRQ 入口汇编（保存寄存器、调用 `clock_irq`、iret/sret）。

策略层（IPL 判据、pending 位图、线程选择、COW、demand paging）不需要任何
条件编译。当前 i386 的后端在 `kernel/arch/i386/hal/`，通用层在
`kernel/hal/`。

---

## 1. 现状与差距

| 能力 | 现状 |
|---|---|
| ring3 被 IRQ 抢占 | ✅ `irq0_entry` → `clock_irq` → `sched_tick`，直接改 iret 帧 |
| 每线程内核栈 | ✅ `kthread_user_spawn()` 分配 8 KB，切换时设 TSS `esp0` |
| 内核态安全点抢占 | ✅ `syscall_dispatch()` 末尾保存 `sysregs` → `proc_sched()` |
| 任意内核点抢占 | ❌ 内核 `IF=0`，IRQ 不会在 syscall 中途到达；即使到达也直接 iret 回去 |

差距的本质：内核态 IRQ 的帧在**当前线程的内核栈**上。要切换，必须保存
“这个线程的内核栈 + 栈上的中断帧”，再在以后恢复它；不能用用户态那套
`*r = *regs` 覆盖 iret 帧。

---

## 2. 目标状态机

每个 `struct kthread` 增加：

```c
	int	t_kpreempted;	/* 1: 内核态被抢占，t_ksp 可恢复 */
	unsigned t_ksp;		/* 被抢占时的内核栈指针 */
	struct sysregs *t_kframe;	/* 内核栈上的 iret 帧，仅调试 */
```

调度器增加一个**独立调度栈**（不能复用任何线程的内核栈）：

```c
#define	SCHED_STACK_SIZE	8192
static unsigned sched_stack_top;
static unsigned sched_esp;	/* kswitch 保存的调度器上下文 */
```

不变量：

1. 只有 `t_kpreempted=1` 的线程才能用 `kswitch(&sched_esp, t->t_ksp)` 恢复；
2. 恢复后必须回到 `clock_irq()` 的 kswitch 之后，继续 `ipl_exit()` 并 iret；
3. 任何时刻只有一个线程在运行；`sched_esp` 只属于调度器；
4. `IPL 15` 区间不抢占、不切换；
5. 内核栈 8 KB 内必须容纳 iret 帧 + clock_irq/sched 的 C 帧（当前约 1 KB）。

---

## 3. 中断入口改动

### 3.1 `irq0_entry`（`usermode_asm.s`）

现在两种来源共用：

```asm
_irq0_entry:
	pushal
	push %es
	push %ds
	movw $0x10,%ax
	movw %ax,%ds
	movw %ax,%es
	pushl %esp
	call _clock_irq
	addl $4,%esp
	pop %ds
	pop %es
	popal
	iret
```

`clock_irq(r)` 已经能通过 `r->cs & 3` 区分 ring3/ring0。
ring0 时**不要**修改 `r`，而是：

```c
if ((r->cs & 3) == 0 && kernel_preempt_allowed()) {
	me->t_kpreempted = 1;
	me->t_ksp = 0;		/* kswitch 填写 */
	me->t_kframe = r;
	/* 切到调度器栈；返回时从这里继续 */
	kswitch(&me->t_ksp, sched_esp);
	/* 被重新调度后回到这里 */
	me->t_kpreempted = 0;
}
```

`kswitch` 保存 callee-saved 寄存器与 esp；`clock_irq` 的 C 帧留在
`me` 的内核栈上，恢复时栈指针回到同一位置，函数正常返回，`irq0_entry`
继续 `pop/iret`。

### 3.2 调度器栈初始化

启动时分配 8 KB（`vfs_page_alloc` + `pmap_map`，和 kstack 相同），
`kswitch` 第一次进入调度器时用 `sched_stack_top` 构造初始帧：

```c
sp = (unsigned *)sched_stack_top;
*--sp = (unsigned)sched_loop;	/* kswitch ret 目标 */
*--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;	/* ebp ebx esi edi */
sched_esp = (unsigned)sp;
```

### 3.3 调度循环

```c
static void
sched_loop(void)
{
	for (;;) {
		struct kthread *t = pick_next();

		if (t == 0) {
			kswitch(&sched_esp, idle_esp);
			continue;
		}
		if (t->t_kpreempted) {
			/* 恢复被抢占的内核上下文 */
			kswitch(&sched_esp, t->t_ksp);
			continue;
		}
		/* 用户态线程：直接 iret 到用户 */
		cur = t->t_proc;
		cur_thread = t;
		cpu_load_cr3(cur->pd);
		tss_set_esp0(t->t_stack_top);
		resume_user(cur->pd, &t->t_regs/p->regs);
	}
}
```

关键：`resume_user` 不返回，调度器的 C 帧被放弃；当该用户线程下一次
在内核态被抢占时，`kswitch(&me->t_ksp, sched_esp)` 会回到 `sched_esp`
保存的位置——即 `sched_loop` 里 `kswitch` 之后，循环继续。这是协作式
协程调度的标准形态。

---

## 4. 允许抢占的判据

不能所有内核点都可抢占，否则会破坏：

- `current_ipl != 0`（临界区/中断禁用区）；
- 正持有自旋锁/正在改共享结构；
- `preempt_point()` 未标注的区域。

实现上：

```c
static int
kernel_preempt_allowed(void)
{
	return (current_ipl == 0 &&
	    cur_thread != 0 &&
	    cur_thread->t_preemptable &&
	    cur_thread->t_base_ipl == 0 &&
	    !in_dispatch);
}
```

进一步可加 `preempt_count`：`preempt_disable()/preempt_enable()` 包住
不可抢占区间；IRQ 只置 `need_resched`，`preempt_enable()` 时再切。

---

## 5. 与现实现的兼容

- ring3 IRQ 路径不变：`sched_tick` 直接改 iret 帧，不需要 kswitch；
- syscall 出口安全点路径保留：它是“已知 `sysregs`”的快路径；
- 内核线程（softirq）继续用 `kthread_block/kthread_poll`，不参与该抢占；
- `IPL 15` 仍屏蔽一切，`ipl_enter(IPL_CLOCK)` 内不切换。

---

## 6. 验证计划（等允许测试时执行）

1. 单元：`ipltest` 不变；
2. 抢占：`/bin/spin` 两进程交错，ticks 增长；
3. 内核点：在 `vfs_namei()` 循环里加 `preempt_point()`，跑 `cat /readme.txt`
   与 `spin` 并发，确认内核栈不串、无 TRAP；
4. 压力：连续 `fork/exec/exit` 100 次，检查 `vstat` 页/vnode 不泄漏；
5. 失败注入：在 `kswitch` 前后加 `KASSERT(t->t_kpreempted)`，确认状态机；
6. 回归：全量套件。

---

## 7. 为什么现在不写

- 中断入口是裸汇编，错误表现为 triple fault，没有串口可调试；
- 调度器栈/`kswitch` 的恢复点必须与 `irq0_entry` 的 pop 顺序严格一致；
- 按当前约束不跑 QEMU，无法确认 iret 帧未被 C 帧覆盖。

**结论**：设计已足够具体到可以照写；等“允许测试”后按 §3–§5 落地，并用
§6 逐项验证。
