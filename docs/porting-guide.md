# 抢占层移植指南（HAL 边界）

> 目标：换架构时**只写后端**，不改调度/IPL/抢占策略。
> 机器可检查：`python tools/check-hal-boundary.py`。

---

## 1. 分层

| 层 | 文件 | 是否随架构改 |
|---|---|---|
| IPL 策略 | `kernel/hal/ipl.c`、`kernel/hal/ipl.h` | 否 |
| 时钟/调度策略 | `kernel/hal/clock.c`、`kernel/hal/clock.h` | 否 |
| 上下文接口 | `kernel/hal/context.h` | 否 |
| 线程/内核栈 | `kernel/thread/kthread.c`、`kthread.h` | 否 |
| 架构后端 | `kernel/arch/<arch>/hal/*.c`、入口汇编 | **是** |

策略层禁止出现：`hal_outb/hal_inb`、`setidt`、`tss_esp0`、`kswitch(`、
`eflags`、`arch/i386`、`PIC1_*/PIC2_*/PIT_CH*`、`irq0_entry`、`IO_ICU*`。
这些由 `tools/check-hal-boundary.py` 静态检查。

---

## 2. 新平台必须实现的钩子

### 2.1 IPL

```c
int  hal_irq_to_ipl(int vector);          /* 平台向量 -> 0..15 */
void hal_ipl_init(void);                  /* 中断控制器初始化，全部屏蔽 */
void hal_ipl_apply(int current_ipl, unsigned enabled_mask);
```

`enabled_mask` 的 bit n 表示 IRQ n 已被驱动注册；`hal_ipl_apply()` 负责把
“未注册 + IPL <= current” 的线屏蔽掉。8259 用两个 mask 寄存器；GIC/APIC
用优先级或 ISR 掩码，语义相同。

### 2.2 时钟

```c
void hal_clock_init(void);   /* 编程定时器，让其中断进入 clock_irq() */
void hal_clock_eoi(void);    /* 中断结束；无 EOI 概念的平台可为空 */
```

定时器频率约定 100 Hz；`clock.c` 负责节拍、`need_resched`、`sched_tick()`。

### 2.3 上下文

```c
void hal_kernel_stack_set(unsigned top);       /* 设置 ring0/内核栈顶 */
void hal_switch_stack(unsigned *old, unsigned new); /* 内核栈切换 */
int  hal_intr_from_user(const struct sysregs *r);   /* 是否来自用户态 */
void hal_intr_user_resume(struct sysregs *r);       /* 让用户上下文可运行 */
unsigned hal_user_stack_top(void);
```

### 2.4 入口汇编

平台需要提供 IRQ0 入口：

1. 保存完整寄存器上下文（布局必须能填 `struct sysregs`）；
2. 切到内核栈（x86 由 TSS 完成，ARM 由 SP_EL1 等完成）；
3. 调用 `clock_irq(struct sysregs *)`；
4. 恢复上下文并返回用户/内核。

`struct sysregs` 目前是 i386 布局；移植到 64 位或 ARM 时，应把它移到
`kernel/arch/<arch>/include/`，策略层只通过 `hal_intr_*` 访问。

---

## 3. 构建接入

以 `build_kernel.ps1` 为例，新增：

```powershell
"ipl_arch.o"     = @("c", "$ws\kernel\arch\$Arch\hal\ipl_arch.c")
"clock_arch.o"   = @("c", "$ws\kernel\arch\$Arch\hal\clock_arch.c")
"context_arch.o" = @("c", "$ws\kernel\arch\$Arch\hal\context_arch.c")
```

并让 `ARCH=<arch>` 选择目录。`build_kernel.sh` 同理。

---

## 4. 移植检查清单

- [ ] `hal_irq_to_ipl()` 覆盖全部中断源，未映射的落到合法级别
- [ ] `hal_ipl_apply()` 在 `current_ipl=15` 时能屏蔽全部可屏蔽中断
- [ ] `hal_clock_init()` 产生 100 Hz 节拍，`hal_clock_eoi()` 正确
- [ ] `hal_kernel_stack_set()` 在每次线程切换前调用
- [ ] `hal_switch_stack()` 保存/恢复 callee-saved 与 SP
- [ ] `hal_intr_from_user()` 正确区分用户/内核来源
- [ ] 入口汇编的帧布局与 `struct sysregs` 一致
- [ ] `python tools/check-hal-boundary.py` 通过
- [ ] `test_ipl` / `test_preempt` / `test_cow` 在新平台全绿

---

## 5. 当前仍与 i386 绑定的部分（不影响抢占策略）

- `struct sysregs` 的寄存器布局（应移到 `arch/<arch>/include/`）
- ELF 加载器与用户栈构造
- 4.5BSD `pmap.c` 的页表格式
- 用户页表操作细节仍通过 `hal_address_space_activate/current` 抽象，
  但底层 `pmap_*` 仍是 i386 实现

系统调用门与地址空间切换已经走 HAL：
`hal_syscall_gate_init()`、`hal_address_space_activate()`、
`hal_address_space_current()`、`hal_cpu_halt()`。
抢占路径本身已完全解耦。
