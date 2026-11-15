# test_cache.ps1 - S17 acceptance: buffer cache + name cache + page cache.
#
# Session: cat /readme.txt, snapshot /dev/vmstat, cat it again, snapshot again.
# On the second pass every byte must come from memory:
#   * buffer-cache hits go up, but disk reads stays exactly the same
#   * page-cache hits go up and fills stays the same (no new pages fetched)
#   * the name cache reports hits
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

function Samples([string]$log, [string]$what, [string]$re) {
    $all = [regex]::Matches($log, $re)
    if ($all.Count -lt 2) {
        Write-Host ("  FAIL need two {0} samples, got {1}" -f $what, $all.Count) -ForegroundColor Red
        $script:fails++
        return $null
    }
    return @($all[$all.Count - 2], $all[$all.Count - 1])
}

Write-Host "S17 acceptance: buffer / name / page caches"
$log = (& $script -Iso $Iso -Keys @(
    "shell",
    "cat /readme.txt",
    "vmstat",
    "cat /readme.txt",
    "vmstat",
    "exit 0") -WaitFor "user exit" -TimeoutSec 120) -join "`n"

Check "cache counters" $log @("buf: total 64", "name: total 64",
                              "page: total 128", "pages: alloc")
Check "file read"      $log @("BSD45-ELF-x86")   # /readme.txt body
Check "exit"           $log @("[user exit 0]")

$b = Samples $log "buffer" 'buf: total (\d+) used (\d+) hits (\d+) misses (\d+) reads (\d+) readahead (\d+) dirty (\d+)'
if ($b -ne $null) {
    $b1h = [int]$b[0].Groups[3].Value; $b1r = [int]$b[0].Groups[5].Value
    $b2h = [int]$b[1].Groups[3].Value; $b2r = [int]$b[1].Groups[5].Value
    Write-Host ("  info buffer hits {0} -> {1}, disk reads {2} -> {3}" -f $b1h, $b2h, $b1r, $b2r)
    if ($b2h -gt $b1h) { Write-Host "  ok   buffer cache served the second pass" -ForegroundColor Green }
    else { Write-Host "  FAIL buffer hits did not grow" -ForegroundColor Red; $fails++ }
    if ($b2r -eq $b1r) { Write-Host "  ok   no extra disk reads on the second pass" -ForegroundColor Green }
    else { Write-Host ("  FAIL disk reads grew {0} -> {1}" -f $b1r, $b2r) -ForegroundColor Red; $fails++ }
}

$p = Samples $log "page" 'page: total (\d+) used (\d+) hits (\d+) misses (\d+) fills (\d+) evicts (\d+)'
if ($p -ne $null) {
    $p1h = [int]$p[0].Groups[3].Value; $p1f = [int]$p[0].Groups[5].Value
    $p2h = [int]$p[1].Groups[3].Value; $p2f = [int]$p[1].Groups[5].Value
    Write-Host ("  info page hits {0} -> {1}, fills {2} -> {3}" -f $p1h, $p2h, $p1f, $p2f)
    if ($p2h -gt $p1h) { Write-Host "  ok   page cache served the second pass" -ForegroundColor Green }
    else { Write-Host "  FAIL page hits did not grow" -ForegroundColor Red; $fails++ }
    if ($p2f -eq $p1f) { Write-Host "  ok   no new page fills on the second pass" -ForegroundColor Green }
    else { Write-Host ("  FAIL page fills grew {0} -> {1}" -f $p1f, $p2f) -ForegroundColor Red; $fails++ }
}

$n = [regex]::Match($log, 'name: total (\d+) used (\d+) hits (\d+) misses (\d+)')
if ($n.Success) {
    $nh = [int]$n.Groups[3].Value
    Write-Host ("  info name-cache hits {0}" -f $nh)
    if ($nh -gt 0) { Write-Host "  ok   name cache hits" -ForegroundColor Green }
    else { Write-Host "  FAIL name cache never hit" -ForegroundColor Red; $fails++ }
} else {
    Write-Host "  FAIL could not parse name-cache counters" -ForegroundColor Red
    $fails++
}

if ($log -match 'TRAP') {
    Write-Host "  FAIL kernel trap" -ForegroundColor Red
    $fails++
}

if ($fails) {
    Write-Host "`nS17 caches: FAILED ($fails)" -ForegroundColor Red
    exit 1
}
Write-Host "`nS17 caches: PASSED" -ForegroundColor Green
exit 0
