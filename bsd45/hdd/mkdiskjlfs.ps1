<#
.SYNOPSIS
    mkdiskjlfs.ps1 - 在宿主机上组装一个"已安装好"的 BSD45 虚拟磁盘（PowerShell 版）
                 功能等价于 mkdiskjlfs.py，产出字节级完全一致。

.DESCRIPTION
    磁盘布局与客户机 install 命令一致：
        LBA 0      : 512 字节 MBR 引导扇区 (build/stage1_hdd.embed.bin)
        LBA 1..8   : stage2 ELF 加载器 (build/stage2.bin)
        LBA 0..6144: JLFS 镜像（其前 8KiB 为 boot block）
        LBA 8192   : 内核 ELF (build/bsd45.elf)
    并 patch MBR 内 "BDMF" 后的 4 个 u32（kernel_lba, kernel_len, stage2_len, 0，小端）。
    使用 .NET System.IO / Buffer.BlockCopy，不调用 python/python3。

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File mkdiskjlfs.ps1 -o build/testdisk.raw
    powershell -NoProfile -ExecutionPolicy Bypass -File mkdiskjlfs.ps1 -o disk.raw --size 67108864 --jlfs hdd/BSD45.JLFS --kernel build/bsd45.elf --stage2 build/stage2.bin --mbr build/stage1_hdd.embed.bin
#>

$HERE = Split-Path -Parent $MyInvocation.MyCommand.Definition
$WS   = Split-Path -Parent $HERE

$SECSZ      = 512
$KERNEL_LBA = 8192
$STAGE2_SECS= 8
$DISK_SIZE  = 64 * 1024 * 1024

# ---------------- 小端 u32 写入 ----------------
function Write-I32 {
    param([byte[]]$b, [int]$off, [long]$v)
    for ($k = 0; $k -lt 4; $k++) {
        $x = ($v -shr (8 * $k)) -band 0xFF
        if ($x -lt 0) { $x += 256 }
        $b[$off + $k] = [byte]$x
    }
}

# 在字节数组中查找字节序列（返回起始下标，找不到返回 -1）
function Find-Signature {
    param([byte[]]$b, [byte[]]$sig)
    $max = $b.Length - $sig.Length
    for ($i = 0; $i -le $max; $i++) {
        $ok = $true
        for ($j = 0; $j -lt $sig.Length; $j++) {
            if ($b[$i + $j] -ne $sig[$j]) { $ok = $false; break }
        }
        if ($ok) { return $i }
    }
    return -1
}

# ---------------- 解析命令行（兼容 Python argparse 写法）----------------
$outPath  = Join-Path $WS (Join-Path 'build' 'testdisk.raw')
$sizeVal  = [long]$DISK_SIZE
$jlfsPath  = Join-Path $HERE 'BSD45.JLFS'
$kernelPath = Join-Path $WS (Join-Path 'build' 'bsd45.elf')
$stage2Path = Join-Path $WS (Join-Path 'build' 'stage2.bin')
$mbrPath  = Join-Path $WS (Join-Path 'build' 'stage1_hdd.embed.bin')

$argv = @($args)
$i = 0
while ($i -lt $argv.Count) {
    $a = [string]$argv[$i]
    if ($a -eq '-o' -or $a -eq '--out') {
        $i++; $outPath = [string]$argv[$i]
    } elseif ($a -eq '--size') {
        $i++; $sizeVal = [long]$argv[$i]
    } elseif ($a -eq '--jlfs') {
        $i++; $jlfsPath = [string]$argv[$i]
    } elseif ($a -eq '--kernel') {
        $i++; $kernelPath = [string]$argv[$i]
    } elseif ($a -eq '--stage2') {
        $i++; $stage2Path = [string]$argv[$i]
    } elseif ($a -eq '--mbr') {
        $i++; $mbrPath = [string]$argv[$i]
    }
    $i++
}

# ---------------- 检查输入文件 ----------------
foreach ($p in @($jlfsPath, $kernelPath, $stage2Path, $mbrPath)) {
    if (-not [System.IO.File]::Exists($p)) {
        [Console]::Error.WriteLine("missing input: $p")
        exit 2
    }
}

$jlfs    = [System.IO.File]::ReadAllBytes($jlfsPath)
$kernel = [System.IO.File]::ReadAllBytes($kernelPath)
$stage2 = [System.IO.File]::ReadAllBytes($stage2Path)
$mbr    = [System.IO.File]::ReadAllBytes($mbrPath)

if ($mbr.Length -ne $SECSZ) {
    [Console]::Error.WriteLine("MBR must be 512 bytes, got $($mbr.Length)")
    exit 2
}
if ($stage2.Length -gt ($STAGE2_SECS * $SECSZ)) {
    [Console]::Error.WriteLine("stage2 too big: $($stage2.Length)")
    exit 2
}
if ($jlfs.Length -gt ($KERNEL_LBA * $SECSZ)) {
    [Console]::Error.WriteLine('JLFS image overlaps the kernel LBA')
    exit 2
}

$img = [System.Array]::CreateInstance([byte], [int]$sizeVal)
[System.Buffer]::BlockCopy($jlfs,    0, $img, 0, $jlfs.Length)
[System.Buffer]::BlockCopy($mbr,    0, $img, 0, $mbr.Length)
[System.Buffer]::BlockCopy($stage2, 0, $img, $SECSZ, $stage2.Length)
$koff = $KERNEL_LBA * $SECSZ
[System.Buffer]::BlockCopy($kernel, 0, $img, $koff, $kernel.Length)

# patch MBR 内的 BDMF manifest
$sig = [byte[]](0x42, 0x44, 0x4D, 0x46)   # 'BDMF'
$idx = Find-Signature $mbr $sig
if ($idx -lt 0) {
    [Console]::Error.WriteLine('MBR has no BDMF manifest')
    exit 2
}
Write-I32 $img ($idx + 4)  $KERNEL_LBA
Write-I32 $img ($idx + 8)  $kernel.Length
Write-I32 $img ($idx + 12) $stage2.Length
Write-I32 $img ($idx + 16) 0

[System.IO.File]::WriteAllBytes($outPath, $img)
Write-Output "disk: $outPath ($($img.Length) bytes)"
Write-Output "  jlfs    $($jlfs.Length) bytes @LBA 0"
Write-Output "  mbr    512 bytes @LBA 0"
Write-Output "  stage2 $($stage2.Length) bytes @LBA 1"
Write-Output "  kernel $($kernel.Length) bytes @LBA $KERNEL_LBA"
exit 0
