#
# Copyright (c) 2026 jialiang.  All rights reserved.
#

# test_cow.ps1 - os.md mechanism 2/3 acceptance: fork() shares pages
# copy-on-write, the child's write is private, and the kernel counters prove
# at least one COW fault and one page copy actually happened.
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

Write-Host "os.md M2/M3 acceptance: COW fork + page-fault copy"
$log = (& $script -Iso $Iso -Keys @(
    "shell",
    "/bin/cowtest",
    "exit 0") -WaitFor "cowtest: PASS" -TimeoutSec 90) -join "`n"

Check "child"  $log @("cowtest: child x=99")
Check "parent" $log @("cowtest: parent x=42")
Check "counters" $log @("cow: shared")
Check "verdict" $log @("cowtest: PASS")

$m = [regex]::Match($log, 'cow: shared (\d+) faults (\d+) copies (\d+)')
if ($m.Success) {
    $shared = [int]$m.Groups[1].Value
    $faults = [int]$m.Groups[2].Value
    $copies = [int]$m.Groups[3].Value
    Write-Host ("  info cow shared={0} faults={1} copies={2}" -f $shared, $faults, $copies)
    if ($shared -gt 0 -and $faults -gt 0 -and $copies -gt 0) {
        Write-Host "  ok   COW path exercised" -ForegroundColor Green
    } else {
        Write-Host "  FAIL COW counters not exercised" -ForegroundColor Red
        $fails++
    }
} else {
    Write-Host "  FAIL could not parse COW counters" -ForegroundColor Red
    $fails++
}

if ($log -match 'kernel TRAP') {
    Write-Host "  FAIL kernel trap" -ForegroundColor Red
    $fails++
}

if ($fails) {
    Write-Host "`nos.md M2/M3 COW: FAILED ($fails)" -ForegroundColor Red
    exit 1
}
Write-Host "`nos.md M2/M3 COW: PASSED" -ForegroundColor Green
exit 0
