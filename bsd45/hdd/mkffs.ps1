<#
.SYNOPSIS
    mkffs.ps1 - 生成最小但合法的 4.4BSD-Lite2 FFS (UFS1) 磁盘镜像（PowerShell 版）
               功能等价于 mkffs.py，产出字节级完全一致。

.DESCRIPTION
    按 4.4BSD-Lite2 sys/ufs/ffs/fs.h、ufs/dinode.h、ufs/dir.h 的 i386 显式字节偏移
    写出 superblock(struct fs)、inode 表(struct dinode)、POSIX 目录(struct direct)，
    并把指定的宿主文件按目录树写入镜像。使用 .NET System.IO / Buffer.BlockCopy，
    所有多字节整数均以小端（i386）手工写入，不调用 python/python3。

.PARAMETER o / --out
    输出镜像路径（必需）。

.PARAMETER size / --size
    镜像总字节数（默认 67108864，即 64MiB）。

.PARAMETER ipg / --ipg
    每个 cylinder group 的 inode 数（默认 128）。

.PARAMETER spec-file
    文本文件，每行一条 "dest=src"，支持 '#' 注释（避免 '/' 开头的参数被改写）。

.PARAMETER files
    位置参数，形如 /bin/sh=sh.elf 的 dest=src 对。

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File mkffs.ps1 -o disk.img -size 67108864 -ipg 128 --spec-file specs.txt
#>

# ---------------- 常量（与 mkffs.py 完全一致）----------------
$DEV_BSIZE = 512
$BSIZE     = 4096
$FSIZE     = 512
$FRAG      = [int]($BSIZE / $FSIZE)          # 8
$BSHIFT    = 12
$FSHIFT    = 9
$FRAGSHIFT = 3
$DEV_BSHIFT= 9
$FSBT_ODB  = 0                               # frag(512B) == device sector(512B)

$IFMT  = 61440                               # 0o170000
$IFDIR = 16384                               # 0o040000
$IFREG = 32768                               # 0o100000
$IFLNK = 40960                               # 0o120000

$INOSIZE = 128
$INOPB   = [int]($BSIZE / $INOSIZE)          # 32
$FS_MAGIC = 0x011954
$NINDIR   = [int]($BSIZE / 4)                # 1024

$DT_UNKNOWN = 0
$DT_DIR     = 4
$DT_REG     = 8

$knownDirs = @('bin','sbin','etc','usr','home','tmp','sys','include',
               'lib','var','dev','proc')

# ---------------- 小端写入辅助 ----------------
function Write-I16 {
    param([byte[]]$b, [int]$off, [int]$v)
    $b[$off]     = [byte]($v -band 0xFF)
    $b[$off + 1] = [byte](($v -shr 8) -band 0xFF)
}
function Write-I32 {
    param([byte[]]$b, [int]$off, [long]$v)
    for ($k = 0; $k -lt 4; $k++) {
        $x = ($v -shr (8 * $k)) -band 0xFF
        if ($x -lt 0) { $x += 256 }
        $b[$off + $k] = [byte]$x
    }
}
function Write-I64 {
    param([byte[]]$b, [int]$off, [long]$v)
    for ($k = 0; $k -lt 8; $k++) {
        $x = ($v -shr (8 * $k)) -band 0xFF
        if ($x -lt 0) { $x += 256 }
        $b[$off + $k] = [byte]$x
    }
}
function Write-U8 {
    param([byte[]]$b, [int]$off, [int]$v)
    $b[$off] = [byte]($v -band 0xFF)
}

# ---------------- build_superblock：i386 显式偏移 ----------------
function Build-Superblock {
    param([long]$totalFrags, [int]$ipg, [int]$ncg, [int]$iblkno,
          [int]$dblkno, [int]$csaddr, [int]$csaddrFrags, [long]$fpg,
          [int]$ndir, [int]$nifree)

    $sb = New-Object byte[] 8192
    $sblkno = [int](8192 / $DEV_BSIZE)             # 16
    $cblkno = $sblkno + [int](8192 / $BSIZE)       # 18

    Write-I32 $sb 0    0
    Write-I32 $sb 4    0
    Write-I32 $sb 8    $sblkno          # fs_sblkno
    Write-I32 $sb 12   $cblkno          # fs_cblkno
    Write-I32 $sb 16   $iblkno          # fs_iblkno
    Write-I32 $sb 20   $dblkno          # fs_dblkno
    Write-I32 $sb 24   0
    Write-I32 $sb 28   0
    Write-I32 $sb 32   0
    Write-I32 $sb 36   $totalFrags      # fs_size
    Write-I32 $sb 40   $totalFrags      # fs_dsize
    Write-I32 $sb 44   $ncg             # fs_ncg
    Write-I32 $sb 48   $BSIZE           # fs_bsize
    Write-I32 $sb 52   $FSIZE           # fs_fsize
    Write-I32 $sb 56   $FRAG            # fs_frag
    Write-I32 $sb 60   0
    Write-I32 $sb 64   0
    Write-I32 $sb 68   0
    Write-I32 $sb 72   (-bnot ($BSIZE - 1))   # fs_bmask
    Write-I32 $sb 76   (-bnot ($FSIZE - 1))   # fs_fmask
    Write-I32 $sb 80   $BSHIFT
    Write-I32 $sb 84   $FSHIFT
    Write-I32 $sb 88   0
    Write-I32 $sb 92   0
    Write-I32 $sb 96   $FRAGSHIFT
    Write-I32 $sb 100  $FSBT_ODB        # fs_fsbtodb
    Write-I32 $sb 104  8192             # fs_sbsize
    Write-I32 $sb 108  0
    Write-I32 $sb 112  0
    Write-I32 $sb 116  $NINDIR          # fs_nindir
    Write-I32 $sb 120  $INOPB           # fs_inopb
    Write-I32 $sb 124  1
    Write-I32 $sb 128  0
    Write-I32 $sb 132  0
    Write-I32 $sb 136  0
    Write-I32 $sb 140  0
    Write-I32 $sb 144  0
    Write-I32 $sb 148  0
    Write-I32 $sb 152  $csaddr          # fs_csaddr
    Write-I32 $sb 156  ($csaddrFrags * $DEV_BSIZE)  # fs_cssize
    Write-I32 $sb 160  $BSIZE           # fs_cgsize
    Write-I32 $sb 164  0
    Write-I32 $sb 168  0
    Write-I32 $sb 172  0
    Write-I32 $sb 176  0
    Write-I32 $sb 180  0
    Write-I32 $sb 184  $ipg             # fs_ipg
    Write-I32 $sb 188  $fpg             # fs_fpg
    # fs_cstotal @192: <iiii> ndir, total_frags, nifree, total_frags
    Write-I32 $sb 192  $ndir
    Write-I32 $sb 196  $totalFrags
    Write-I32 $sb 200  $nifree
    Write-I32 $sb 204  $totalFrags
    Write-U8  $sb 208  0                # fs_fmod
    Write-U8  $sb 209  1                # fs_clean
    Write-U8  $sb 210  0                # fs_ronly
    Write-U8  $sb 211  0                # fs_flags
    Write-U8  $sb 212  ([int][char]'/') # fs_fsmnt (剩余保持 0)
    Write-I32 $sb 724  0                # fs_cgrotor
    # fs_csp[31] @728..851, fs_maxcluster @852 -> 保持 0
    Write-I32 $sb 856  0                # fs_cpc
    # fs_opostbl[16][8] @860..1115, fs_sparecon[50] @1116..1315 -> 0
    Write-I32 $sb 1316 0                # fs_contigsumsize
    Write-I32 $sb 1320 0                # fs_maxsymlinklen
    Write-I32 $sb 1324 2                # fs_inodefmt
    Write-I64 $sb 1328 ([long]::MaxValue)            # fs_maxfilesize = (1<<63)-1
    Write-I64 $sb 1336 (-bnot ($BSIZE - 1))          # fs_qbmask = ~(BSIZE-1)
    Write-I64 $sb 1344 (-bnot ($FSIZE - 1))          # fs_qfmask = ~(FSIZE-1)
    Write-I32 $sb 1352 0                # fs_state
    Write-I32 $sb 1356 0                # fs_postblformat
    Write-I32 $sb 1360 0                # fs_nrpos
    Write-I32 $sb 1364 0                # fs_postbloff
    Write-I32 $sb 1368 0                # fs_rotbloff
    Write-I32 $sb 1372 $FS_MAGIC        # fs_magic
    return $sb
}

# ---------------- build_dinode：128 字节 ----------------
function Build-Dinode {
    param([int]$mode, [int]$nlink, [long]$size, [long]$blocks,
          $direct, $indirect)

    $b = New-Object byte[] 128
    # Hh: mode(u16), nlink(i16)
    Write-I16 $b 0 ($mode -band 0xFFFF)
    Write-I16 $b 2 $nlink
    Write-I32 $b 4 0                    # di_u.oldids[2]
    Write-I64 $b 8 $size                # di_size
    # atime/atimensec, mtime/mtimensec, ctime/ctimensec (6 x i32 = 0)
    for ($k = 0; $k -lt 6; $k++) { Write-I32 $b (16 + 4 * $k) 0 }
    # di_db[12]
    for ($i = 0; $i -lt 12; $i++) {
        $val = 0
        if (($null -ne $direct) -and ($i -lt $direct.Count)) { $val = $direct[$i] }
        Write-I32 $b (40 + 4 * $i) $val
    }
    # di_ib[3]
    for ($i = 0; $i -lt 3; $i++) {
        $val = 0
        if (($null -ne $indirect) -and ($i -lt $indirect.Count)) { $val = $indirect[$i] }
        Write-I32 $b (88 + 4 * $i) $val
    }
    Write-I32 $b 100 0                  # di_flags
    Write-I32 $b 104 $blocks            # di_blocks
    Write-I32 $b 108 1                  # di_gen
    Write-I32 $b 112 0                  # di_uid
    Write-I32 $b 116 0                  # di_gid
    Write-I32 $b 120 0                  # di_spare[0]
    Write-I32 $b 124 0                  # di_spare[1]
    return $b
}

# ---------------- dirent：一条 struct direct ----------------
function dirent {
    param([int]$ino, [int]$dtype, [string]$name)
    $raw = [System.Text.Encoding]::ASCII.GetBytes($name)
    $namlen = $raw.Length
    $reclen = (8 + $namlen + 1 + 3) -band (-bnot 3)   # DIRSIZ 4 对齐
    $out = New-Object byte[] $reclen
    Write-I32 $out 0 $ino
    Write-I16 $out 4 $reclen
    Write-U8  $out 6 $dtype
    Write-U8  $out 7 $namlen
    [System.Buffer]::BlockCopy($raw, 0, $out, 8, $namlen)
    return $out
}

# ---------------- normalize_dest：还原 MSYS/Git-Bash 改写 ----------------
function Normalize-Dest {
    param([string]$dest)
    $dest = $dest.Replace('\', '/')
    if ($dest -notmatch '^[A-Za-z]:[/\\]') { return $dest }
    $parts = @()
    foreach ($p in ($dest -split '/')) {
        if ($p -eq '') { continue }
        if ($p -match '^[A-Za-z]:$') { continue }
        $parts += $p
    }
    for ($i = $parts.Count - 1; $i -ge 0; $i--) {
        if ($knownDirs -contains $parts[$i]) {
            $tail = $parts[$i..($parts.Count - 1)]
            return '/' + ($tail -join '/')
        }
    }
    if ($parts.Count -gt 0) { return '/' + $parts[$parts.Count - 1] }
    return '/'
}

# ---------------- 解析命令行（兼容 Python argparse 写法）----------------
$outPath  = $null
$sizeVal  = [long](64 * 1024 * 1024)
$ipg      = 128
$specFile = $null
$posFiles = @()

$argv = @($args)
$i = 0
while ($i -lt $argv.Count) {
    $a = [string]$argv[$i]
    if ($a -eq '-o' -or $a -eq '--out') {
        $i++; $outPath = [string]$argv[$i]
    } elseif ($a -eq '-size' -or $a -eq '--size') {
        $i++; $sizeVal = [long]$argv[$i]
    } elseif ($a -eq '-ipg' -or $a -eq '--ipg') {
        $i++; $ipg = [int]$argv[$i]
    } elseif ($a -eq '--spec-file') {
        $i++; $specFile = [string]$argv[$i]
    } else {
        $posFiles += $a
    }
    $i++
}

if ([string]::IsNullOrEmpty($outPath)) {
    [Console]::Error.WriteLine('mkffs.ps1: -o/--out is required')
    exit 2
}

# ---------------- 载入 spec 列表 ----------------
$specs = @()
$specs += $posFiles
if ($null -ne $specFile) {
    if (-not [System.IO.File]::Exists($specFile)) {
        [Console]::Error.WriteLine("mkffs.ps1: missing spec-file: $specFile")
        exit 2
    }
    foreach ($raw in [System.IO.File]::ReadAllLines($specFile)) {
        $line = $raw.Trim()
        if ($line -eq '' -or $line.StartsWith('#')) { continue }
        $specs += $line
    }
}

$totalFrags = [long]([math]::Floor($sizeVal / $FSIZE))   # // FSIZE
$ncg = 1
$fpg = $totalFrags

# 固定布局（以 frag 为单位）
$iblkno      = 32
$inode_frags = [int]($ipg / $INOPB) * $FRAG
$dblkno      = $iblkno + $inode_frags
$csaddr      = 24
$csaddrFrags = 1

# 解析为 (dest, data) 列表
$entries = @()
foreach ($spec in $specs) {
    $eq = $spec.IndexOf('=')
    if ($eq -lt 0) {
        [Console]::Error.WriteLine("mkffs.ps1: bad spec (need dest=src): $spec")
        exit 2
    }
    $dest = Normalize-Dest ($spec.Substring(0, $eq))
    $src  = [System.Environment]::ExpandEnvironmentVariables($spec.Substring($eq + 1))
    if (-not [System.IO.File]::Exists($src)) {
        [Console]::Error.WriteLine("mkffs.ps1: missing source: $src")
        exit 2
    }
    $data = [System.IO.File]::ReadAllBytes($src)
    $entries += @{ dest = $dest; data = $data }
}

$image = [System.Array]::CreateInstance([byte], [int]$sizeVal)

# ---------------- 构建目录树 ----------------
# nodes: path -> hashtable(ino,type,children,[size],[direct],[indirect],[indir_frag],[nblocks],[frag])
$nodes = [ordered]@{}
$nodes['/'] = @{ ino = 2; type = $IFDIR; children = [System.Collections.ArrayList]::new() }
$next_ino = 3
$file_data  = [System.Collections.ArrayList]::new()   # @{ off; data }
$indir_data = [System.Collections.ArrayList]::new()   # @{ frag; ptrs }
$script:next_frag = $dblkno

function alloc_frag {
    $f = $script:next_frag
    $script:next_frag = $f + $FRAG
    return $f
}

$holder = [int[]]@($next_ino)
foreach ($ent in $entries) {
    $dest = $ent.dest
    $data = $ent.data
    if (-not $dest.StartsWith('/')) { $dest = '/' + $dest }
    $parts = @($dest -split '/' | Where-Object { $_ -ne '' })
    if ($parts.Count -eq 0) { continue }

    $parent = '/'
    for ($j = 0; $j -lt $parts.Count - 1; $j++) {
        $p = $parts[$j]
        $child = $parent.TrimEnd('/') + '/' + $p
        if (-not $nodes.Contains($child)) {
            $dirIno = $holder[0]
            $holder[0] = $holder[0] + 1
            $nodes[$child] = @{ ino = $dirIno; type = $IFDIR; children = [System.Collections.ArrayList]::new() }
            [void]$nodes[$parent]['children'].Add(@($p, $IFDIR, $dirIno))
        }
        $parent = $child
    }
    $name = $parts[$parts.Count - 1]
    $ino  = $holder[0]
    $holder[0] = $holder[0] + 1

    $nblocks = [int][math]::Floor(($data.Length + $BSIZE - 1) / $BSIZE)
    if ($nblocks -eq 0) { $nblocks = 1 }

    $direct = @()
    for ($k = 0; $k -lt [math]::Min($nblocks, 12); $k++) { $direct += (alloc_frag) }
    $indirect = @()
    $indir_frag = 0
    if ($nblocks -gt 12) {
        $indir_frag = alloc_frag
        $extra = @()
        for ($k = 0; $k -lt ($nblocks - 12); $k++) { $extra += (alloc_frag) }
        $indirect = $extra
        [void]$indir_data.Add(@{ frag = $indir_frag; ptrs = $extra })
    }
    for ($k = 0; $k -lt $direct.Count; $k++) {
        $blk = $direct[$k]
        $start = $k * $BSIZE
        $len = [math]::Min($BSIZE, $data.Length - $start)
        if ($len -gt 0) {
            $chunk = New-Object byte[] $len
            [System.Buffer]::BlockCopy($data, $start, $chunk, 0, $len)
            [void]$file_data.Add(@{ off = ($blk * $DEV_BSIZE); data = $chunk })
        }
    }
    for ($k = 0; $k -lt $indirect.Count; $k++) {
        $blk = $indirect[$k]
        $start = (12 + $k) * $BSIZE
        $len = [math]::Min($BSIZE, $data.Length - $start)
        if ($len -gt 0) {
            $chunk = New-Object byte[] $len
            [System.Buffer]::BlockCopy($data, $start, $chunk, 0, $len)
            [void]$file_data.Add(@{ off = ($blk * $DEV_BSIZE); data = $chunk })
        }
    }
    $nodes[$dest] = @{ ino = $ino; type = $IFREG; size = $data.Length;
                       direct = $direct; indirect = $indirect;
                       indir_frag = $indir_frag; nblocks = $nblocks }
    [void]$nodes[$parent]['children'].Add(@($name, $IFREG, $ino))
}
$next_ino = $holder[0]

# root '..' 指向自身；子目录 '..' 指向父目录
foreach ($path in @($nodes.Keys)) {
    $node = $nodes[$path]
    if ($node.type -ne $IFDIR) { continue }
    if ($path -ne '/') {
        $slash = $path.LastIndexOf('/')
        $parent = if ($slash -gt 0) { $path.Substring(0, $slash) } else { '/' }
        [void]$node.children.Insert(0, @('..', $IFDIR, $nodes[$parent]['ino']))
    }
    [void]$node.children.Insert(0, @('.', $IFDIR, $node['ino']))
}

# ---------------- 写目录数据块 ----------------
foreach ($path in @($nodes.Keys)) {
    $node = $nodes[$path]
    if ($node.type -ne $IFDIR) { continue }
    $ms = New-Object System.IO.MemoryStream
    foreach ($c in $node.children) {
        $dt = if ($c[1] -eq $IFDIR) { $DT_DIR } else { $DT_REG }
        $de = [byte[]](dirent ([int]$c[2]) $dt ([string]$c[0]))
        $ms.Write($de, 0, $de.Length)
    }
    if ($ms.Length -gt $BSIZE) {
        [Console]::Error.WriteLine("mkffs.ps1: directory too big: $path")
        exit 2
    }
    $fragNo = alloc_frag
    $blob = $ms.ToArray()
    [System.Buffer]::BlockCopy($blob, 0, $image, ($fragNo * $DEV_BSIZE), $blob.Length)
    $node.frag = $fragNo
    $node.size = $blob.Length
}

# ---------------- 写间接块（单间接指针数组）----------------
foreach ($id in $indir_data) {
    $arr = New-Object byte[] $BSIZE
    $ptrs = $id.ptrs
    $lim = [math]::Min($ptrs.Count, $NINDIR)
    for ($k = 0; $k -lt $lim; $k++) { Write-I32 $arr ($k * 4) $ptrs[$k] }
    [System.Buffer]::BlockCopy($arr, 0, $image, ($id.frag * $DEV_BSIZE), $BSIZE)
}

# ---------------- 写文件数据 ----------------
foreach ($fd in $file_data) {
    [System.Buffer]::BlockCopy($fd.data, 0, $image, $fd.off, $fd.data.Length)
}

# ---------------- 写 inode 表 ----------------
foreach ($path in @($nodes.Keys)) {
    $node = $nodes[$path]
    $ino = $node.ino
    if ($ino -le 0 -or $ino -gt $ipg) { continue }
    $off = $iblkno * $DEV_BSIZE + ($ino - 1) * $INOSIZE
    if ($node.type -eq $IFDIR) {
        $direct = @($node.frag)
        $indirect = @()
        $nblocks = if ($node.size) { $FRAG } else { 0 }
    } else {
        $direct = $node.direct
        $indirect = $node.indirect
        $nblocks = $node.nblocks * $FRAG
    }
    $indir_ptrs = if ($node.indir_frag) { @($node.indir_frag) } else { @() }
    if ($node.type -eq $IFDIR) {
        $mode = $IFDIR -bor 493      # 0o755
        $nlink = 2
    } else {
        $mode = $IFREG -bor 420      # 0o644
        $nlink = 1
    }
    $din = [byte[]](Build-Dinode $mode $nlink ([long]$node.size) ([long]$nblocks) $direct $indir_ptrs)
    [System.Buffer]::BlockCopy($din, 0, $image, $off, 128)
}

# ---------------- superblock ----------------
$ndir = 0
foreach ($node in $nodes.Values) { if ($node.type -eq $IFDIR) { $ndir++ } }
$nifree = $ipg - $nodes.Count
$sb = [byte[]](Build-Superblock $totalFrags $ipg $ncg $iblkno $dblkno $csaddr $csaddrFrags $fpg $ndir $nifree)
[System.Buffer]::BlockCopy($sb, 0, $image, 8192, 8192)

# ---------------- cg summary（只读挂载所需的最小内容）----------------
$cg = New-Object byte[] $BSIZE
Write-I32 $cg 0 $script:next_frag
[System.Buffer]::BlockCopy($cg, 0, $image, ($csaddr * $DEV_BSIZE), $BSIZE)

# ---------------- 检查 ----------------
if ($image.Length -ne $sizeVal) {
    [Console]::Error.WriteLine("mkffs.ps1: image overflow: used $($image.Length) > size $sizeVal")
    exit 2
}
if ($script:next_frag -gt $totalFrags) {
    [Console]::Error.WriteLine("mkffs.ps1: out of fragments: used $($script:next_frag) > $totalFrags")
    exit 2
}

[System.IO.File]::WriteAllBytes($outPath, $image)
Write-Output "FFS image: $outPath ($($image.Length) bytes)"
Write-Output ("  superblock @8192  magic=0x{0:x6}  bsize={1} fsize={2}" -f $FS_MAGIC, $BSIZE, $FSIZE)
Write-Output "  inodes: $($nodes.Count)/$ipg  inode_table@frag$iblkno  data@frag$dblkno"
exit 0
