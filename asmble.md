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

## 当前状态审计（2026-11-20 更新：已全部合规）

`bsd45/kernel/` 下所有汇编源码现已全部位于 `arch/` 树内。审计命令输出为空：
非 `arch/` 目录既无 `.s/.S/.asm` 文件，也无 C/头文件内联汇编。

### 合规（全部在 arch/）

| 文件 | 说明 |
|---|---|
| `kernel/arch/i386/ap_tramp.s` | AP 启动 trampoline（S200 已加入构建） |
| `kernel/arch/i386/usermode_asm.s` | `resume_user` / `irq0_entry`（2026-11-20 从 `sys/` 迁入） |
| `kernel/arch/i386/kswitch.s` | 内核栈切换（2026-11-20 从 `thread/` 迁入） |
| `kernel/arch/i386/i386/locore.s` | 引导早期、页表、上下文切换汇编 |
| `kernel/arch/i386/isa/locore.s` | 机器相关低层例程 |
| `kernel/arch/i386/isa/icu.s` | 8259 中断控制器 stub |
| `kernel/arch/i386/x86.asm` | 端口 I/O 与 CPU 控制（hal_inb/outb 等） |
| `kernel/arch/sparc/{mul,rem,sdiv,udiv,umul,urem}.s` | SPARC 算术（2026-11-20 从 `lib/sparc/` 迁入，不参与 i386 构建） |

### 2026-11-20 整改记录

1. **S200 同步原语**：`sys/sync.h` 初稿把 atomic 用 `__asm` 内联写在通用头里，
   违反本规则。已迁出：`kernel/arch/i386/atomic.c`（实现）+ `atomic.h`（声明），
   `sync.h` 仅保留数据结构/函数声明，`#include "arch/i386/atomic.h"`。
2. **历史遗留 8 个汇编文件全部迁移**：`usermode_asm.s`、`kswitch.s` →
   `arch/i386/`；6 个 SPARC `.s` → `arch/sparc/`。同步更新了
   `build_kernel.sh`/`.ps1`、`kernel/{sys,thread,arch/i386}/config.mk` 的引用，
   README 相关描述一并订正。
3. **验证**：Windows（gcc→PE→COFF→objcopy→ELF）完整重编译无
   `undefined reference`；`synctest`/`ipitest`/`kthreadtest` PASS（证明
   `kswitch.s` 栈切换正常），`test_s3` PASSED（证明 `usermode_asm.s`
   的 `resume_user` 正常进用户态 shell）。

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
