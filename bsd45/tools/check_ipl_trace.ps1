#
# Copyright (c) 2026 jialiang.  All rights reserved.
#

# check_ipl_trace.ps1 - independent replay of the kernel's IPL transition
# trace.  This is deliberately a log-only checker: it does not trust the
# kernel self-test verdict, it re-derives the os.md invariants:
#
#   R (raise):   to > from
#   X (restore): to <= from
#   E (irq in):  to > from, push from
#   I (irq out): to == popped from
#   D (dispatch): to > from, ipending bit 'to' set, push from
#   d (done):     to == popped from
#
# It also requires sequence numbers to increase and levels to stay in 0..15.
param([Parameter(Mandatory=$true)][string]$Log)

$lines = Get-Content -Path $Log | Where-Object {
    $_ -match '^\s*itrace:\s+(\d+)\s+([RXEIDd])\s+(\d+)\s+(\d+)\s+([0-9a-fA-F]+)'
}
if ($lines.Count -eq 0) {
    Write-Host "  FAIL ipl trace: no events found" -ForegroundColor Red
    exit 1
}

$ev = @()
foreach ($l in $lines) {
    if ($l -match '^\s*itrace:\s+(\d+)\s+([RXEIDd])\s+(\d+)\s+(\d+)\s+([0-9a-fA-F]+)') {
        $ev += [pscustomobject]@{
            Seq = [int]$matches[1]; Kind = $matches[2]
            From = [int]$matches[3]; To = [int]$matches[4]
            Pend = [Convert]::ToUInt32($matches[5], 16)
        }
    }
}

$errs = 0
$prevSeq = -1
$estack = New-Object System.Collections.ArrayList
$dstack = New-Object System.Collections.ArrayList
$checked = 0
foreach ($e in $ev) {
    if ($e.Seq -le $prevSeq) {
        Write-Host ("  FAIL seq not increasing at {0}" -f $e.Seq) -ForegroundColor Red
        $errs++
    }
    $prevSeq = $e.Seq
    if ($e.From -lt 0 -or $e.From -gt 15 -or $e.To -lt 0 -or $e.To -gt 15) {
        Write-Host ("  FAIL level out of range seq {0}" -f $e.Seq) -ForegroundColor Red
        $errs++
        continue
    }
    switch -CaseSensitive ($e.Kind) {
        'R' { if (-not ($e.To -gt $e.From)) { Write-Host ("  FAIL R seq {0}: {1}->{2}" -f $e.Seq,$e.From,$e.To) -ForegroundColor Red; $errs++ } }
        'X' { if (-not ($e.To -le $e.From)) { Write-Host ("  FAIL X seq {0}: {1}->{2}" -f $e.Seq,$e.From,$e.To) -ForegroundColor Red; $errs++ } }
        'E' {
            if (-not ($e.To -gt $e.From)) { Write-Host ("  FAIL E not up seq {0}" -f $e.Seq) -ForegroundColor Red; $errs++ }
            [void]$estack.Add($e.From)
        }
        'I' {
            if ($estack.Count -gt 0) {
                $want = $estack[$estack.Count - 1]
                if ($e.To -ne $want) { Write-Host ("  FAIL I seq {0}: want {1} got {2}" -f $e.Seq,$want,$e.To) -ForegroundColor Red; $errs++ }
                $estack.RemoveAt($estack.Count - 1)
            }
        }
        'D' {
            if (-not ($e.To -gt $e.From)) { Write-Host ("  FAIL D not up seq {0}" -f $e.Seq) -ForegroundColor Red; $errs++ }
            if ((($e.Pend -shr $e.To) -band 1) -ne 1) { Write-Host ("  FAIL D pending bit missing seq {0} ipl {1}" -f $e.Seq,$e.To) -ForegroundColor Red; $errs++ }
            [void]$dstack.Add($e.From)
        }
        'd' {
            if ($dstack.Count -gt 0) {
                $want = $dstack[$dstack.Count - 1]
                if ($e.To -ne $want) { Write-Host ("  FAIL d seq {0}: want {1} got {2}" -f $e.Seq,$want,$e.To) -ForegroundColor Red; $errs++ }
                $dstack.RemoveAt($dstack.Count - 1)
            }
        }
    }
    $checked++
}

# a full 256-entry ring may start or end mid-transition; report but only
# fail when a complete trace leaves unmatched frames and the ring wrapped.
if ($estack.Count -ne 0 -or $dstack.Count -ne 0) {
    if ($ev.Count -lt 256) {
        Write-Host ("  FAIL unmatched frames: irq {0}, dispatch {1}" -f $estack.Count, $dstack.Count) -ForegroundColor Red
        $errs++
    } else {
        Write-Host ("  info trace ring wrapped; unmatched head frames ignored") -ForegroundColor Yellow
    }
}

if ($errs) {
    Write-Host ("check_ipl_trace: FAILED ({0} error(s), {1} events)" -f $errs, $checked) -ForegroundColor Red
    exit 1
}
Write-Host ("check_ipl_trace: PASS ({0} events, nesting and order verified)" -f $checked) -ForegroundColor Green
exit 0

