# BSD45 — 从 ISO 安装到 VMware 虚拟磁盘（操作指南）

2026-11-18 在 VMware Workstation 17（Windows 主机）上实测通过。

## 已验证的结果（本次实测）

1. **从 ISO 安装**：从 `BSD45-ELF-x86.iso` 启动，monitor 里执行 `install`，
   把 ISO 内的 FFS 文件系统（`BSD45.FFS`，3 MiB）、stage2、ELF 内核
   （约 518 KiB）和 MBR 写进 VMware 虚拟磁盘。串口日志实证：
   ```
   install: FFS written (3145728 bytes)
   install: kernel written (530860 bytes, 1037 sectors at LBA 8192)
   install: SUCCESS - BSD45 is on the virtual disk.
   ```
2. **从虚拟磁盘启动**：断开 ISO 后直接从该 VMDK 启动，日志实证：
   ```
   ffs: mounted bsize 4096 fsize 512 frag 8
   ata: ATA disk 131072 sectors, FFS mounted
   root: FFS on ATA disk (no ATAPI CD-ROM)
   BSD/386 monitor - type 'help' for commands
   bsd45>
   ```
   即：系统不再依赖光盘，根文件系统来自虚拟磁盘，monitor/`shell` 正常运行。

同样的流程也在 QEMU + raw 磁盘上通过（`tools/test_hdd_install.ps1`，
phase1 安装 + phase2 从盘启动，两阶段均 PASSED）。

## 本次构建链（Windows / PowerShell）

`.ps1` 是 Windows 构建链，`.sh` 是 Ubuntu 构建链。Windows 侧内核走的是：

```
gcc -m32 编译  ->  COFF 目标文件
ld -m i386pe (两遍链接, 确定 _end/_edata/_etext)  ->  kernel.pe
objcopy -O elf32-i386 -R .idata -R .reloc  ->  build/bsd45.elf
```

一键构建：
```powershell
cd C:\Users\Administrator\Desktop\work\bsd45
$env:HTTPS_PROXY=""; $env:https_proxy=""; $env:HTTP_PROXY=""; $env:http_proxy=""
powershell -NoProfile -ExecutionPolicy Bypass -File .\build_all.ps1
```

> 注意：主机若设了 `HTTPS_PROXY`/`https_proxy`（大小写两份），PowerShell 的
> `Start-Process` 会因大小写重复键抛 `ArgumentException`，构建前先清空这 4 个变量。
> `ld` 报的 `section below image base` 是 PE 链接的已知警告，不影响产物。

## 两份虚拟机配置怎么用

`vms/` 目录里有两个 `.vmx`，对应两个阶段：

### A. 直接启动已装好的系统（推荐，用来看"能跑起来"）

打开 **`BSD45-diskboot.vmx`**。它挂的是已经装好系统的
`BSD45-installed.vmdk`（含 FFS 根 + 内核 + MBR），不挂光驱，BIOS 从硬盘启动。
开机即可看到 `ffs: mounted` → `root: FFS on ATA disk` → `bsd45>` 提示符。
在提示符敲 `shell` 可进入 `/bin/sh`（`ls /bin`、`echo` 等可用）。

### B. 从 ISO 重新安装一遍（重现安装过程）

打开 **`BSD45-install.vmx`**。它挂：
- 光驱 `ide1:0` → `..\out\BSD45-ELF-x86.iso`（live CD）
- 空白盘 `ide0:0` → `BSD45-blank.vmdk`（64 MiB）

BIOS 从光驱启动，进 monitor 后依次敲：
```
disk        # 确认看到 ATA disk 131072 sectors (64 MiB)
install     # 把 ISO 里的 FFS/内核/stage2/MBR 写进空白盘
```
看到 `install: SUCCESS` 后关闭虚拟机。这块 `BSD45-blank.vmdk` 就变成了已装好的盘；
若想用它启动，把 `BSD45-diskboot.vmx` 里的 `ide0:0.fileName` 改成 `BSD45-blank.vmdk`，
或直接用现成的 `BSD45-installed.vmdk`。

### 键鼠注意

客户机内核轮询 i8042 键盘（无 USB 栈）。两个 vmx 已强制
`keyboard.vusb.enable = "FALSE"` / `mouse.vusb.enable = "FALSE"`。
启动后先点一下 VM 窗口再敲键盘。

### 串口日志

两个 vmx 都开了 `serial0.fileType = "file"`，日志写到同目录
`bsd45-boot-serial.log` / `bsd45-install-serial.log`，可随时查看启动过程。
（Windows 主机上 named-pipe 串口无法可靠地用 PowerShell 读取，故用 file 模式。）

## 文件清单（vms/）

| 文件 | 用途 |
|---|---|
| `BSD45-diskboot.vmx` | 从已装好的虚拟磁盘启动（主交付） |
| `BSD45-installed.vmdk` + `BSD45-installed-flat.vmdk` | 已装好系统的 64 MiB 虚拟磁盘 |
| `BSD45-install.vmx` | 从 ISO 安装流程（live CD + 空白盘） |
| `BSD45-blank.vmdk` | 安装用的空白 64 MiB 虚拟磁盘 |
| `BSD45-ELF-x86.vmx` | 原始配置：仅从 ISO 启动到 monitor（老的验证用） |
| `bsd45-boot-serial.log` | 本次从盘启动的串口日志（验证证据） |

## 磁盘布局（512 字节扇区，superfloppy MBR）

```
LBA 0        MBR = stage1_hdd（内嵌 BDMF manifest: 内核 LBA/长度、stage2 长度）
LBA 1..8     stage2（32 位 ELF 加载器，8 扇区保留）
LBA 16..     FFS 超级块（镜像 byte 8192）与整个 FFS 镜像
LBA 8192..   裸 ELF 内核（stage1 加载到 0x10000）
```
