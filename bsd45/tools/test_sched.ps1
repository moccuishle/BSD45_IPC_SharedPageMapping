# test_sched.ps1 - verify the 20-level priority scheduler.
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

Write-Host "Priority scheduler acceptance: 20-level + inheritance"
$log = (& $script -Iso $Iso -Keys @(
    "schedtest",
    "kthread") -WaitFor "20-level prio + inheritance PASS" -TimeoutSec 60) -join "`n"

Check "levels"      $log @("schedtest: 20-level prio + inheritance PASS")

if ($log -match 'TRAP') {
    Write-Host "  FAIL kernel trap" -ForegroundColor Red
    $fails++
}

if ($fails) {
    Write-Host "`nS priority: FAILED ($fails)" -ForegroundColor Red
    exit 1
}
Write-Host "`nS priority: PASSED" -ForegroundColor Green
exit 0
