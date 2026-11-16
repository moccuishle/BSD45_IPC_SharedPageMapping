#
# Copyright (c) 2026 jialiang.  All rights reserved.
#

# test_badptr.ps1 - os.md mechanism 1 negative test: copyin/copyout access
# checks turn unmapped and read-only user pointers into -EFAULT instead of
# a kernel fault.
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

Write-Host "os.md M1 acceptance: access_ok / EFAULT"
$log = (& $script -Iso $Iso -Keys @(
    "shell",
    "/bin/badptr",
    "exit 0") -WaitFor "user exit" -TimeoutSec 90) -join "`n"

Check "unmapped" $log @("badptr: unmapped write -> -14")
Check "read-only" $log @("badptr: read into text -> -14")
Check "verdict"  $log @("badptr: PASS")

if ($log -match 'kernel TRAP') {
    Write-Host "  FAIL kernel trap" -ForegroundColor Red
    $fails++
}

if ($fails) {
    Write-Host "`nos.md M1 badptr: FAILED ($fails)" -ForegroundColor Red
    exit 1
}
Write-Host "`nos.md M1 badptr: PASSED" -ForegroundColor Green
exit 0
