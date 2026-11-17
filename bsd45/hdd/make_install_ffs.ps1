<#
.SYNOPSIS
    make_install_ffs.ps1 - 生成客户机 `install` 写入 VMware 虚拟盘的 FFS 镜像（PowerShell 版）
                          功能等价于 make_install_ffs.py，产出字节级完全一致。

.DESCRIPTION
    mkffs.ps1 的薄包装：从 out/userland 挑选 init/sh/hello 及可用的核心工具，
    把 dest=src 列表写入临时 spec 文件，再以固定 3MiB 调用 mkffs.ps1。
    所有 on-disk 格式逻辑仅在 mkffs.ps1 中。本脚本不调用 python/python3。

.PARAMETER output.img
    可选输出路径；默认 hdd/BSD45.FFS。

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File make_install_ffs.ps1 [output.img]
#>

$HERE = Split-Path -Parent $MyInvocation.MyCommand.Definition
$WS   = Split-Path -Parent $HERE
$USERLAND = Join-Path $WS 'out/userland'

$out = if ($args.Count -gt 0) { [string]$args[0] } else { Join-Path $HERE 'BSD45.FFS' }

# dest-on-disk -> userland ELF 文件名
$FILES = @(
    @{ dest = '/sbin/init'; name = 'init.elf' },
    @{ dest = '/bin/sh';    name = 'sh.elf' },
    @{ dest = '/hello';     name = 'hello.elf' }
)

# 可选核心工具（仅当已构建时纳入）
$TOOLS = @('echo','ls','cat','mkdir','rm','cp','mv','wc','grep','head','tail','df',
           'mount','ps','uname','sleep','kill','true','false','date','vmstat','test')

$specs = [System.Collections.ArrayList]::new()

foreach ($f in $FILES) {
    $src = Join-Path $USERLAND $f.name
    if ([System.IO.File]::Exists($src)) {
        [void]$specs.Add(@($f.dest, $src))
    } else {
        [Console]::Error.WriteLine("make_install_ffs.ps1: WARNING missing $src")
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
    [Console]::Error.WriteLine('make_install_ffs.ps1: init.elf or sh.elf missing in out/userland')
    exit 2
}

# 写入 spec 文件（'/' 开头的 dest 不会被改写）
$specPath = [System.IO.Path]::GetTempFileName()
try {
    $lines = @()
    foreach ($s in $specs) { $lines += ($s[0] + '=' + $s[1]) }
    $text = ($lines -join "`n") + "`n"
    [System.IO.File]::WriteAllText($specPath, $text, (New-Object System.Text.UTF8Encoding($false)))

    $mkffs = Join-Path $HERE 'mkffs.ps1'
    & $mkffs -o $out -size 3145728 --spec-file $specPath
    $rc = $LASTEXITCODE
} finally {
    if ([System.IO.File]::Exists($specPath)) {
        Remove-Item -LiteralPath $specPath -Force -ErrorAction SilentlyContinue
    }
}
exit $rc
