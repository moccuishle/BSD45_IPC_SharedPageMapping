#
# Copyright (c) 2026 jialiang.  All rights reserved.
#

# test_subsys.ps1 - S20 acceptance: API personalities decoupled from kernel.
#
# Kernel side: `subsystest` exercises the four registered personalities.
# User side:   /bin/subtest switches personality and calls each ABI.
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

Write-Host "S20 acceptance: API subsystems (unix/posix/linux/win32)"
$log = (& $script -Iso $Iso -Keys @(
    "subsystest",
    "subsys",
    "shell",
    "/bin/subtest",
    "exit 0") -WaitFor "user exit" -TimeoutSec 90) -join "`n"

Check "kernel registry" $log @("subsystest: 4 personalities ok")
Check "kernel verdict"  $log @("subsystest: PASS")
Check "active default"  $log @("subsys: 4 personalities, active unix")
Check "unix ABI"        $log @("subtest: unix pid=1")
Check "posix ABI"       $log @("subtest: posix pid=1")
Check "linux ABI"       $log @("subtest: linux pid=1", "linux-write-ok")
Check "win32 ABI"       $log @("subtest: win32 pid=1")
Check "user verdict"    $log @("subtest: PASS")
Check "exit"            $log @("[user exit 0]")

if ($log -match 'TRAP') {
    Write-Host "  FAIL kernel trap" -ForegroundColor Red
    $fails++
}

if ($fails) {
    Write-Host "`nS20 subsystems: FAILED ($fails)" -ForegroundColor Red
    exit 1
}
Write-Host "`nS20 subsystems: PASSED" -ForegroundColor Green
exit 0
