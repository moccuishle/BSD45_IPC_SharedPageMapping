#
# Copyright (c) 2026 jialiang.  All rights reserved.
#

# test_fault.ps1 - os.md mechanism 1 acceptance: a user-mode fault kills
# only the faulting process (SIGSEGV) and the shell keeps running.
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

Write-Host "os.md M1 acceptance: user fault isolation"
$log = (& $script -Iso $Iso -Keys @(
    "shell",
    "/bin/crash",
    "echo alive",
    "exit 0") -WaitFor "user exit" -TimeoutSec 90) -join "`n"

Check "fault"    $log @("crash: about to fault")
Check "signal"   $log @("user fault: pid", "sig 11")
Check "survived" $log @("alive")
Check "exit"     $log @("[user exit 0]")

if ($log -match 'kernel TRAP') {
    Write-Host "  FAIL kernel trap" -ForegroundColor Red
    $fails++
}

if ($fails) {
    Write-Host "`nos.md M1 fault: FAILED ($fails)" -ForegroundColor Red
    exit 1
}
Write-Host "`nos.md M1 fault: PASSED" -ForegroundColor Green
exit 0
