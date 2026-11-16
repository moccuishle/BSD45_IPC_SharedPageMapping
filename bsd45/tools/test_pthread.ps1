#
# Copyright (c) 2026 jialiang.  All rights reserved.
#

# test_pthread.ps1 - S26 acceptance: user pthread_create/join/detach.
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

Write-Host "S26 acceptance: pthread_create / pthread_join / pthread_detach"
$log = (& $script -Iso $Iso -Keys @(
    "shell",
    "/bin/pthread",
    "exit 0") -WaitFor "user exit" -TimeoutSec 120) -join "`n"

Check "create"  $log @("pthreadtest: created 4 threads")
Check "join"    $log @("pthreadtest: counter=4000 join=ok")
Check "detach"  $log @("pthreadtest: detach counter=5000")
Check "verdict" $log @("pthreadtest: PASS")

if ($log -match 'TRAP') {
    Write-Host "  FAIL kernel trap" -ForegroundColor Red
    $fails++
}

if ($fails) {
    Write-Host "`nS26 pthread: FAILED ($fails)" -ForegroundColor Red
    exit 1
}
Write-Host "`nS26 pthread: PASSED" -ForegroundColor Green
exit 0
