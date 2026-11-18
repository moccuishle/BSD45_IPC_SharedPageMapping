# Windows 开发环境安装指南（MSYS2 + gcc + qemu）

> 来源：2026-11-13 在本机的实战记录。新机器装环境时，按本文顺序执行即可，文末附踩坑记录。

## 环境清单

| 组件 | 版本参考 | 路径 | 仓库 |
|------|---------|------|------|
| MSYS2 | 最新版 | `C:\msys64` | — |
| GCC | 16.x | `C:\msys64\ucrt64\bin\gcc.exe` | ucrt64 |
| QEMU | 11.x | `C:\msys64\ucrt64\bin\qemu-system-x86_64.exe` | ucrt64 |
| binutils（objcopy/ld/ar/as/nm/objdump 等） | 2.47 | 随 gcc 自动作为依赖安装 | ucrt64 |
| bison | 3.8.2 | `C:\msys64\usr\bin\bison.exe` | msys |
| flex | 2.6.4 | `C:\msys64\usr\bin\flex.exe` | msys |
| xorriso | 1.5.8 | `C:\msys64\usr\bin\xorriso.exe` | msys |

## 一键复现步骤

### 1. 下载并安装 MSYS2（静默安装）

```bash
# 下载（约 81MB，国内推荐 TUNA）
curl -L -o "$USERPROFILE/Downloads/msys2-x86_64-latest.exe" \
  https://mirrors.tuna.tsinghua.edu.cn/msys2/distrib/msys2-x86_64-latest.exe

# 静默安装到 C:\msys64
"$USERPROFILE/Downloads/msys2-x86_64-latest.exe" install --root C:/msys64 --confirm-command
```

⚠️ 安装器可能会在最后初始化阶段卡住或被中断，**先验证**再继续：

```bash
# 验证核心文件存在
ls C:/msys64/usr/bin/pacman.exe C:/msys64/usr/bin/bash.exe
```

文件在就算安装成功，初始化问题后面有绕过办法。

### 2. 配置镜像源（清华 TUNA，实测完整可靠）

编辑 `C:\msys64\etc\pacman.d\mirrorlist.mingw`：

```
Server = https://mirrors.tuna.tsinghua.edu.cn/msys2/mingw/ucrt64/
```

编辑 `C:\msys64\etc\pacman.d\mirrorlist.msys`：

```
Server = https://mirrors.tuna.tsinghua.edu.cn/msys2/msys/x86_64/
```

### 3. 绕过签名校验（关键！）

编辑 `C:\msys64\etc\pacman.conf`，找到 `SigLevel`，改为：

```
#SigLevel    = Required
SigLevel    = Never
```

同时把用不到的仓库注释掉，只保留（本环境实测可行）：

```
[ucrt64]
Include = /etc/pacman.d/mirrorlist.mingw

[msys]
Include = /etc/pacman.d/mirrorlist.msys
```

### 4. 同步数据库并安装 gcc + qemu

```bash
C:/msys64/usr/bin/pacman.exe -Sy
C:/msys64/usr/bin/pacman.exe -S --noconfirm --needed \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-qemu
```

约 2~3 分钟（含依赖共数百 MB）。binutils（objcopy、ld 等）会自动装上，无需单独装。

### 5. 验证

```bash
C:/msys64/ucrt64/bin/gcc.exe --version
C:/msys64/ucrt64/bin/qemu-system-x86_64.exe --version
C:/msys64/ucrt64/bin/objcopy.exe --version
C:/msys64/ucrt64/bin/ld.exe --version
```

### 6. 最终任务：一次性安装 gcc / qemu / flex / bison / xorriso

前面第 4 步只装了 gcc + qemu。要一次装齐全部五个工具，用下面这条命令（gcc/qemu 是 ucrt64 包，bison/flex/xorriso 是 msys 包，pacman 可一次混合安装）：

```bash
C:/msys64/usr/bin/pacman.exe -S --noconfirm --needed \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-qemu \
  bison flex xorriso
```

- `--needed` 保证已装且是最新版的包会跳过，重复执行安全（幂等）
- binutils（objcopy/ld/ar/as/nm/objdump）随 gcc 自动装上，无需单独装；m4 随 bison 自动装上
- bison / flex / xorriso 是 **msys 版**，可执行文件在 `C:\msys64\usr\bin\`，不在 `ucrt64\bin`（见坑 5）

验证五个工具（前四个在 ucrt64\bin，后三个在 usr\bin）：

```bash
C:/msys64/ucrt64/bin/gcc.exe --version
C:/msys64/ucrt64/bin/qemu-system-x86_64.exe --version
C:/msys64/usr/bin/bison.exe --version
C:/msys64/usr/bin/flex.exe --version
C:/msys64/usr/bin/xorriso.exe --version
```

本机实测结果（2026-11-19，均已安装）：gcc 16.2.0、qemu 11.1.3、binutils 2.47、bison 3.8.2、flex 2.6.4、xorriso 1.5.8.pl02。

## 日常使用

- 打开 `C:\msys64\ucrt64.exe` 终端直接用 `gcc` / `qemu-system-x86_64` / `objcopy`
- 或把 `C:\msys64\ucrt64\bin` 加入系统 PATH，任意终端可用
- 装其它工具：`pacman -S --noconfirm mingw-w64-ucrt-x86_64-<包名>`（如 gdb、make、ninja）

### 7. 可选：安装 mingw32（i686）工具链

BSD45 内核需要 32 位工具链。默认装的 ucrt64 是 x86_64（gcc 靠 `-m32` 兼容），若要一套**原生 i686** 的 gcc/binutils，装 `mingw-w64-i686-gcc`（含 binutils 2.47，装在 `C:\msys64\mingw32\bin`）。MSYS2 的构建脚本 `tools-find.ps1` 会按 `mingw32 → ucrt64 → mingw64` 顺序找 gcc，装好后会自动优先用它。

⚠️ **装这个包默认会失败**（`target not found` 或同步时报 404），因为 `pacman.conf` 里 `[mingw32]` 仓库默认是**注释掉**的，且现有的 `mirrorlist.mingw` 里只有 ucrt64 的 URL。按下面三步配好源再装：

```bash
# 1) 给 mingw32 建独立的镜像清单。TUNA 的 i686 路径实测 404，
#    因此官方源放第一优先，TUNA 仅作备选。
cat > C:/msys64/etc/pacman.d/mirrorlist.mingw32 <<'EOF'
Server = https://repo.msys2.org/mingw/i686/
Server = https://mirrors.tuna.tsinghua.edu.cn/msys2/mingw/i686/
EOF

# 2) 编辑 C:\msys64\etc\pacman.conf，把这两行的注释去掉/改成：
#    [mingw32]
#    Include = /etc/pacman.d/mirrorlist.mingw32

# 3) 强制刷新数据库并安装
C:/msys64/usr/bin/pacman.exe -Syy
C:/msys64/usr/bin/pacman.exe -S --noconfirm --needed mingw-w64-i686-gcc
```

验证：

```bash
C:/msys64/mingw32/bin/gcc.exe --version
C:/msys64/mingw32/bin/objcopy.exe --version   # GNU Binutils 2.47
```

> **重要结论（避坑）**：装了 i686 工具链后，BSD45 内核仍是 ~572 KB（`.bss` 仍只有 9 KB），与用 ucrt64 编译的结果一样。根因不是 CPU 位数，而是 **MinGW 的 PE target 与 Linux ELF target 对 `static buf[N]` 这类数组的段处理不同**——MinGW（无论 i686 还是 x86_64）都把大数组放进 `.data`（占磁盘），Linux ELF 工具链放进 `.bss`（不占磁盘）。所以**靠换 mingw32 解决不了"PE 内核过大导致虚拟机里 install 卡住"的问题**；要产出自举可用的内核 ISO，得用 Linux ELF 交叉工具链（Ubuntu 上 `i686-w64-mingw32-*` + `objcopy -O elf32-i386` 已实测可生成 123 KB 的精简内核）。 mingw32 工具链只是备着，日常 ucrt64 够用。

---

## 踩坑记录（新机器遇到同样问题时直接对号入座）

> 环境安装类问题在本文；**内核尺寸异常、install 报 FAILED 这类构建期问题**已移到 `hole_trap.md`（原坑 10、坑 11）。

### 坑 1：pacman 报 "Public keyring not found" 后卡住
- **原因**：安装器最后一步 bash 初始化被中断，`pacman-key --init` 没跑过
- **对策**：不要尝试修 keyring，直接按上面第 3 步设 `SigLevel = Never` 绕过

### 坑 2：pacman-key --init 死循环刷 "removing stale lockfile"
- **原因**：gpg 在本机环境生成主密钥会卡死（bash -lc、msys2.exe 两种方式都复现）
- **对策**：同上，`SigLevel = Never`。这是选择绕过的根本原因，**不要再浪费时间试 pacman-key**

### 坑 3：南大镜像（mirror.nju.edu.cn/msys2）大量 404
- **现象**：.db 和部分包文件 404；它家 mingw 仓库还不支持 `$arch` 变量（要用固定路径）
- **对策**：直接用 TUNA 镜像（实测完整）。备选：官方 `repo.msys2.org` 也验证过可用；中科大 403 不可用
- **排查手法**：用 curl 直接探测具体包文件 URL（返回 200 才算有），不要信镜像名气

### 坑 4：长任务用 shell `&` 放后台会随命令结束被杀
- **对策**：用持久后台任务机制跑 pacman，输出保留下来便于检查

### 坑 5：bison / flex 装完在 `ucrt64\bin` 里找不到
- **现象**：`C:\msys64\ucrt64\bin\bison.exe`、`flex.exe` 不存在，`pacman -S mingw-w64-ucrt-x86_64-bison ...` 报找不到包
- **原因**：官方仓库根本没有 bison / flex 的 ucrt64 变体（ucrt64 下只有 `flexdll`，那是另一个东西），bison / flex 只提供 msys 版
- **对策**：直接装 msys 版 `pacman -S --noconfirm --needed bison flex`，可执行文件在 `C:\msys64\usr\bin\`（bison 3.8.2、flex 2.6.4，m4 作为依赖自动装）。在 `C:\msys64\ucrt64.exe` 终端里 `/usr/bin` 已在 PATH 中，可直接调用；不要在 `ucrt64\bin` 里白找

### 坑 6：普通 PowerShell/cmd 里直接调 gcc.exe 编译静默失败
- **现象**：`gcc.exe --version` 正常，但真编译 hello.c 时退出码 1、无任何输出，像是 gcc 坏了
- **原因**：`cc1.exe` 找不到依赖 DLL（`libgmp-10.dll`、`libisl-23.dll`、`libmpc-3.dll`、`libmpfr-6.dll`、`libz-1.dll` 等，文件都齐全，只是不在 DLL 搜索路径）；`--version` 不走 cc1，所以不受影响，极具迷惑性
- **对策**：日常直接用 `C:\msys64\ucrt64.exe` 终端操作（PATH 已配好，最省事）；若必须在普通终端调 gcc，先 `$env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH` 把该目录加到 PATH 最前面再编译

### 坑 7：PowerShell 5.1 跑 build_all.ps1 在 genassym 步骤崩溃
- **现象**：构建到 genassym 时 `Start-Process` 抛 `ArgumentException: 字典中已存在键 http_proxy / HTTP_PROXY`，随后 `genassym run failed`，`BUILD_ALL_EXIT=1`
- **原因**：PowerShell 5.1 的 `Start-Process` 会把进程环境当字典传递，环境里同时存在仅大小写不同的 `http_proxy` 与 `HTTP_PROXY`（以及 `https_proxy`/`HTTPS_PROXY`）时会因重复键报错。这与项目代码无关，是 Windows 环境的通病
- **对策**：运行构建前先删掉小写那组，再用 `powershell` 启动脚本（子进程继承干净环境）：

```powershell
foreach ($n in @('http_proxy','https_proxy','no_proxy')) { Remove-Item "env:$n" -ErrorAction SilentlyContinue }
& powershell -NoProfile -ExecutionPolicy Bypass -File ".\build_all.ps1"
```

- 实测这样处理后 `build_all.ps1` 全链路通过，产出 `out\BSD45-ELF-x86.iso`（xorriso 打包）。ld 的 `section below image base` 是 PE 链接的无害警告，可忽略

### 坑 8：装 i686 工具链报 `target not found: mingw-w64-i686-gcc`
- **现象**：直接 `pacman -S mingw-w64-i686-gcc` 报 target not found；`-Ssq` 搜不到包；`-Sy` 同步时只下载 ucrt64/msys，没有 mingw32
- **原因**：`C:\msys64\etc\pacman.conf` 里 `[mingw32]` 段整段被注释（`#` 开头），且共享的 `mirrorlist.mingw` 里只有 ucrt64 一行 URL，没有 i686 源。本机当初配环境只启用了 ucrt64 + msys 两个仓库
- **对策**：见上面「7. 安装 mingw32」的三步——建独立 `mirrorlist.mingw32`、在 pacman.conf 取消 `[mingw32]` 注释并指过去、`-Syy` 强制刷新后再装。**注意改完要 `-Syy`（两个 y）**，只 `-Sy` 不会重新拉数据库

### 坑 9：TUNA 的 i686 镜像路径 404
- **现象**：给 `mirrorlist.mingw32` 只写 `https://mirrors.tuna.tsinghua.edu.cn/msys2/mingw/i686/` 后，`-Syy` 报 `failed retrieving file 'mingw32.db' ... 404`
- **原因**：TUNA 该路径下没有 i686 仓库（本文档第 2 步配的 TUNA 只覆盖 ucrt64 和 msys）
- **对策**：改用官方 `https://repo.msys2.org/mingw/i686/` 作第一源（实测可下载安装成功），TUNA 留作备选。不要因为前面 TUNA 对 ucrt64 好用就默认它对 i686 也好使——**每个仓库路径要单独验**

## 排查卡住命令的通用流程

1. **输出落盘**：`命令 > log 2>&1 &`，然后 `tail` 看它在等什么
2. **限时换路**：一个方法卡超过 1 分钟就换（先想"能不能绕过"，再想"怎么修"）
3. **直接验证**：curl 探文件、`--debug` 看实际 URL、`pacman -Q` 查已装包
4. **横向对比**：一个源坏了同时测多个备选，选实测能用的
