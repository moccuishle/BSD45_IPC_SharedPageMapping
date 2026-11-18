<#
.SYNOPSIS
    make_install_jlfs.ps1 - 生成"原生 JLFS"安装镜像（3 MiB），供客户机 install 写入 VMware 虚拟盘。

.DESCRIPTION
    原 make_install_ffs.ps1 的 JLFS 对应版：同样从 out/userland 挑选 init/sh/hello 及核心工具，
    写 spec 后调用 mkjlfs.ps1（而非 mkffs.ps1），以固定 3 MiB 产出 BSD45.JLFS。

    mkjlfs.ps1 生成的镜像与 mkffs.ps1 唯一区别在 CG 块：写入 CG magic 0x4a4c4653 与
    inode/frag 位图（规则与 kernel/fs/jlfs/jlfs.c:cg_rebuild() 一致），因此 jlfs_mount()
    不打印 "rebuilding allocation bitmap"，挂载即以读写方式使用。

    原本的 make_install_ffs.ps1 / mkffs.ps1 完全不动。

.PARAMETER output.img
    可选输出路径，默认 hdd/BSD45.JLFS。

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File make_install_jlfs.ps1 [output.img]
#>

$HERE = Split-Path -Parent $MyInvocation.MyCommand.Definition
$WS   = Split-Path -Parent $HERE
$USERLAND = Join-Path $WS 'out/userland'

$out = if ($args.Count -gt 0) { [string]$args[0] } else { Join-Path $HERE 'BSD45.JLFS' }

# dest-on-disk -> userland ELF 文件名（与 make_install_jlfs.ps1 相同）
$FILES = @(
    @{ dest = '/sbin/init'; name = 'init.elf' },
    @{ dest = '/bin/sh';    name = 'sh.elf' },
    @{ dest = '/hello';     name = 'hello.elf' }
)
$TOOLS = @('echo','ls','cat','mkdir','rm','cp','mv','wc','grep','head','tail','df',
           'mount','ps','uname','sleep','kill','true','false','date','vmstat','test')

$specs = [System.Collections.ArrayList]::new()
foreach ($f in $FILES) {
    $src = Join-Path $USERLAND $f.name
    if ([System.IO.File]::Exists($src)) {
        [void]$specs.Add(@($f.dest, $src))
    } else {
        [Console]::Error.WriteLine("make_install_jlfs.ps1: WARNING missing $src")
    }
}
foreach ($t in $TOOLS) {
    $src = Join-Path $USERLAND ($t + '.elf')
    if ([System.IO.File]::Exists($src)) {
        [void]$specs.Add(@(('/bin/' + $t), $src))
    }
}

$hasSh   = $false
$hasInit = $false
foreach ($s in $specs) {
    if ($s[0] -eq '/bin/sh')    { $hasSh = $true }
    if ($s[0] -eq '/sbin/init') { $hasInit = $true }
}
if (-not $hasSh -or -not $hasInit) {
    [Console]::Error.WriteLine('make_install_jlfs.ps1: init.elf or sh.elf missing in out/userland')
    exit 2
}

$specPath = [System.IO.Path]::GetTempFileName()
try {
    $lines = @()
    foreach ($s in $specs) { $lines += ($s[0] + '=' + $s[1]) }
    $text = ($lines -join "`n") + "`n"
    [System.IO.File]::WriteAllText($specPath, $text, (New-Object System.Text.UTF8Encoding($false)))

    $mkjlfs = Join-Path $HERE 'mkjlfs.ps1'
    & powershell -NoProfile -ExecutionPolicy Bypass -File $mkjlfs -o $out -size 3145728 --spec-file $specPath
    $rc = $LASTEXITCODE
} finally {
    if ([System.IO.File]::Exists($specPath)) {
        Remove-Item -LiteralPath $specPath -Force -ErrorAction SilentlyContinue
    }
}
exit $rc
