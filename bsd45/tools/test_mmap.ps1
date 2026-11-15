# test_mmap.ps1 - S17 acceptance: file/anonymous/shared mmap and msync.
#
# /bin/mmaptest checks three things and prints one line per case:
#   mmap: file ok    - MAP_PRIVATE of /readme.txt matches read(2)
#   mmap: anon ok    - MAP_ANON memory is readable/writable
#   mmap: shared ok  - MAP_SHARED of a tmpfs file + msync is visible via read(2)
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

Write-Host "S17 acceptance: mmap + page cache"
$log = (& $script -Iso $Iso -Keys @(
    "shell",
    "/bin/mmaptest",
    "exit 0") -WaitFor "user exit" -TimeoutSec 90) -join "`n"

Check "private file map" $log @("mmap: file ok size=1066 match=1")
Check "anonymous map"    $log @("mmap: anon ok")
Check "shared + msync"   $log @("mmap: shared ok")
Check "overall"          $log @("mmap: ok")
Check "exit"             $log @("[user exit 0]")

if ($log -match 'TRAP') {
    Write-Host "  FAIL kernel trap" -ForegroundColor Red
    $fails++
}

if ($fails) {
    Write-Host "`nS17 mmap: FAILED ($fails)" -ForegroundColor Red
    exit 1
}
Write-Host "`nS17 mmap: PASSED" -ForegroundColor Green
exit 0
