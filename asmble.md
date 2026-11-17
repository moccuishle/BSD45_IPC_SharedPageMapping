# 汇编源码目录规则（asmble）

> 工程规则，适用于本仓库所有工程（`bsd45/` 及后续新增工程的内核树）。
> 建立日期：2026-11-18。

## 规则

**内核（`kernel/`）下的所有汇编源码，只允许放在 `arch/` 目录树下。**

- 允许：`kernel/arch/<arch>/` 及其子目录，例如
  `kernel/arch/i386/`、`kernel/arch/i386/isa/`、`kernel/arch/sparc/`。
- 禁止：`kernel/sys/`、`kernel/thread/`、`kernel/lib/`、`kernel/fs/`、
  `kernel/vm/`、`kernel/driver/` 等任何非 `arch/` 目录。
- 受管文件后缀：`.s`、`.S`、`.asm`。

## 为什么

1. **架构相关性**：汇编直接操作寄存器、栈帧、特权指令、上下文切换，
   本质上与 CPU 架构绑定。放进 `arch/<arch>/` 才能让移植新架构时
   只替换一个目录。
2. **构建隔离**：构建脚本按 `ARCH_<arch>` 选择 `arch/<arch>/` 下的汇编
   与 C 源；汇编散落在通用目录会让"换架构"变成到处改路径。
3. **审阅边界**：看到 `arch/` 就知道要按目标架构分别验证；看到通用目录
   就默认它应该是可移植 C。汇编出现在通用目录通常是设计走样的信号。
4. **与当前内核模型一致**：本内核已把端口 I/O、CPU 控制、上下文切换钩子
   分别收敛到 `arch/i386/io.h`（声明）+ `arch/i386/x86.asm`（实现）、
   `arch/i386/ctx.c`、`arch/i386/timer.c`，本规则是同一原则的延续。

## 当前状态审计（2026-11-18）

`bsd45/kernel/` 下共 **13** 个汇编文件，其中 **5 个合规 / 8 个待迁移**：

### 合规（已在 arch/）

| 文件 | 说明 |
|---|---|
| `kernel/arch/i386/ap_tramp.s` | AP 启动 trampoline（SMP 基础设施，未加入构建） |
| `kernel/arch/i386/i386/locore.s` | 引导早期、页表、上下文切换汇编 |
| `kernel/arch/i386/isa/locore.s` | 机器相关低层例程 |
| `kernel/arch/i386/isa/icu.s` | 8259 中断控制器 stub |
| `kernel/arch/i386/x86.asm` | 端口 I/O 与 CPU 控制（hal_inb/outb 等） |

### 待迁移（7 个）

| 当前位置 | 建议目标 | 说明 |
|---|---|---|
| `kernel/sys/usermode_asm.s` | `kernel/arch/i386/usermode_asm.s` | `resume_user` / `irq0_entry`，x86 栈帧与 iret 语义 |
| `kernel/thread/kswitch.s` | `kernel/arch/i386/kswitch.s` | `kswitch` 内核栈切换，直接操作 esp/ebx/esi/edi |
| `kernel/lib/sparc/mul.s` | `kernel/arch/sparc/mul.s` | SPARC 乘法 |
| `kernel/lib/sparc/rem.s` | `kernel/arch/sparc/rem.s` | SPARC 取余 |
| `kernel/lib/sparc/sdiv.s` | `kernel/arch/sparc/sdiv.s` | SPARC 有符号除法 |
| `kernel/lib/sparc/udiv.s` | `kernel/arch/sparc/udiv.s` | SPARC 无符号除法 |
| `kernel/lib/sparc/umul.s` | `kernel/arch/sparc/umul.s` | SPARC 无符号乘法 |
| `kernel/lib/sparc/urem.s` | `kernel/arch/sparc/urem.s` | SPARC 无符号取余 |

（上表 SPARC 为 6 个；加上 `usermode_asm.s`、`kswitch.s` 共 8 条目标，
其中 SPARC 6 个当前不参与 i386 构建。）

## 执行要求

1. **新增汇编**：一律写入对应 `kernel/arch/<arch>/`，不得放在其他目录。
2. **迁移既有文件**：移动源码的同时，必须同步更新所有引用它的构建脚本
   （`build_kernel.ps1`/`.sh`、`build_iso.ps1`/`.sh` 及任何 `config.mk`），
   并重新构建 + 跑回归测试，确认无 `undefined reference`。
3. **C 侧声明随之调整**：汇编实现的函数声明应放在
   `kernel/arch/<arch>/` 下的头文件（如 `io.h`、`kthread.h` 中的
   `kswitch` 原型例外需注明），不要在通用 `sys/` 头里声明架构专属汇编符号。
4. **例外**：确需例外的，必须在本文件"例外清单"中登记理由与评审人，
   否则视为违规。

## 检查方法

```bash
# 列出所有"在 kernel 下但不在 arch 下"的汇编源码（应为空）
find kernel -type f \( -name '*.s' -o -name '*.S' -o -name '*.asm' \) \
  | grep -v '/arch/' || echo "OK: 所有汇编都在 arch 下"
```

新工程建立时，把这条检查加入 `verify.sh` / `verify.ps1` 或 CI。

## 例外清单

（暂无。新增例外须写明文件、理由、评审人、日期。）
