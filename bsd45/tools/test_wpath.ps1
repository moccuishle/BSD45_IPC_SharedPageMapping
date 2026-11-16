#
# Copyright (c) 2026 jialiang.  All rights reserved.
#

# test_wpath.ps1 - S29 acceptance: kstr path through the VFS.
param([string]$Iso = "")

$ErrorActionPreference = "Stop"
$ws = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (!$Iso) { $Iso = Join-Path $ws "out\BSD45-ELF-x86.iso" }

$script = Join-Path $ws "tools\run_qemu_test.ps1"
$fails = 0

function Check([string]$name, [string]$log, [string[]]$needles) {
    foreach ($n in $needles) {
        if ($log.Contains($n)) {
            Write-Host ("  ok   {0}: found {1}" -f $name, $n) -ForegroundColor Green
        } else {
            Write-Host ("  FAIL {0}: missing {1}" -f $name, $n) -ForegroundColor Red
            $script:fails++
        }
    }
}

Write-Host "S29 acceptance: kstr path lookup (ASCII + UTF-16 builds)"
$log = (& $script -Iso $Iso -Keys @("wtest") -WaitFor "wtest: PASS" -TimeoutSec 60) -join "`n"

Check "lookup"  $log @("wtest: kstr path")
Check "verdict" $log @("wtest: PASS")

if ($log -match 'TRAP') {
    Write-Host "  FAIL kernel trap" -ForegroundColor Red
    $fails++
}

if ($fails) {
    Write-Host "`nS29 wpath: FAILED ($fails)" -ForegroundColor Red
    exit 1
}
Write-Host "`nS29 wpath: PASSED" -ForegroundColor Green
exit 0
