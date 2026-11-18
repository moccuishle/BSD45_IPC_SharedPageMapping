# BSD45 Windows 构建踩坑记录（内核尺寸 / 安装失败）

> 从 `ai_install_gccenv.md` 的「踩坑记录」中拆分出来的两个构建期问题。
> 编号沿用原文档（坑 10、坑 11），方便和旧笔记/提交信息对照。
> 环境安装类的问题（pacman、镜像源、签名、mingw32 安装等）仍在 `ai_install_gccenv.md`。

---

## 坑 10：Windows 编出的内核比 Ubuntu 大 4 倍，install 直接卡死

- **现象**：同一份源码，Ubuntu 编译的 `bsd45.elf` 只有 123,544 字节，Windows/MSYS2 编出来 572,328 字节；用这个内核打出的 ISO 在 VMware 里敲 `install` 后**完全没有输出**，卡死。
- **定位过程**（三步就能锁定）：
  1. `objdump -h` 对比两个 ELF：Windows 版 `.data` = 411 KB、`.bss` = 9 KB；Ubuntu 版 `.data` = 10 KB、`.bss` = 419 KB —— **同一批数据换了段**。
  2. 用 `nm` 统计符号所在段：**349 个 static 变量全在 `.data`，95 个全局变量全在 `.bss`**，规律非常干净。
  3. `objdump -h build/obj/sys/stubs.o` 看到 `static char heap[256KB]` 落在 **`.data$heap`** 段，且 `stubs.o` 文件自己就 266 KB —— 零初始化数据真的被写进了文件。
- **根因**：`i686-w64-mingw32-gcc` 是 **PE/COFF 目标**。开了 `-fdata-sections` 后，所有 *static* 的零初始化变量被放进 `.data$<name>` 段并**真实携带全零内容**；Linux ELF 工具链则放进 `.bss`（SHT_NOBITS，不占文件）。所以**跟 32/64 位无关，装 i686 工具链解决不了**。
- **为什么会导致 install 卡死**：bootloader 按 manifest 里的 `kernelSize`（就是 ELF 文件大小）把内核整块读进内存。572 KB 时加载失败 → ISO 挂载失败 → 敲 install 毫无反应。
- **对策（已内置进构建流程）**：`tools/pe_bss_fix.py` 在链接前扫一遍所有 `.o`，把满足「段名 `.data$*` + 无重定位 + 内容全零」的段改写成 BSS 段：`VirtualSize` 保留原大小、`SizeOfRawData` 置 0、标志改为 `CNT_UNINITIALIZED_DATA`，并把段名改成短名 `.bss$<序号>`。这是纯表示层改写（全零且无重定位的段按定义就是未初始化数据），语义完全等价。
  - `build_kernel.ps1` 链接前自动调用；`kernel/Makefile.win` 也加了同一步。
  - 效果：`.data` 411 KB → 10 KB，ELF 572 KB → 146 KB，`readelf -S` 里 `.bss` 变成 `NOBITS`。
- **两个必须注意的细节**（踩过才知道）：
  1. **只改段属性不够**。PE 的输出节**不能含空洞**，如果段名仍是 `.data$xxx`，链接器照样把它并进 `.data` 并把零写进磁盘。必须**改名成 `.bss$xxx`** 才会并入 `.bss` 输出节。
  2. **段名不能原地改 string table**：`.data$`（6 字节）比 `.bss$`（5 字节）长，原地替换会移位，破坏所有 `/<offset>` 引用和符号名。所以要直接写进段头那 8 字节内联字段（`.bss$<序号>` 最多 8 字符）。
- **根治方向**（本机编译器跑不起来，未实测）：去掉 `-fdata-sections` 让 GCC 回到默认的 `.bss`，或改用真正的 ELF 交叉工具链（`i686-elf-gcc`）；MSYS2 自带的 ld 只支持 `i386pe` 一种 emulation，`-m elf_i386` 走不通。

---

## 坑 11：install 走到一半报 `FAILED (see messages above)`，实际是 embed.o 陈旧

- **现象**：内核起来、`install` 也跑起来了，日志一路正常：
  ```
  install: writing stage2 (0 bytes) at LBA 1
  install: writing /bsd45.elf to disk ...
  install: kernel written (146336 bytes, 286 sectors at LBA 8192)
  install: bad stage1_hdd size 4
  install: FAILED (see messages above)
  ```
- **定位**：`stage2 (0 bytes)` 就是信号。查 `build/embed.o` 只有 1652 字节，而 `build/embed.c` 里有 hello(10320)+init(10508)+stage1_hdd(512)+stage2(196) 共 21 KB 数据 —— **embed.o 是旧文件**（02:03）而 embed.c 是 04:23 重新生成的，`.o` 从没跟着更新，里面的 `stage2_bin` 只有 4 字节。
- **后果**：`install_to_disk()` 第 4 步校验 `stage1_hdd_bin_len != SECSZ` 直接返回 -1，MBR 都没写，磁盘装不起来。
- **对策**：
  1. 正常环境：重跑一次完整 `build_kernel.ps1` 即可（它会无条件重编 embed.o）。**分步/带 `-Only` 编译时最容易漏掉 embed.o**，检查 `ls -l build/embed.o` 应约 22 KB。
  2. 编译器跑不起来时（cc1 被拦但 `as.exe` 可用）：`python tools/embedc_to_s.py build/embed.c build/embed_new.s && as --32 -o build/embed.o build/embed_new.s`，把 `embed.c` 的数组重新汇编成目标文件，不需要编译器。
- **验证**：链接后核对 `_stage1_hdd_bin_len` 应为 512、`_stage2_bin_len` 应为 196（`readelf -s` + 读 .data 段即可）。
