# test_diskboot.ps1 - boot an already-installed BSD45 raw disk (no CD-ROM)
# and dump the serial console.  Fast loop for the FFS-root bring-up:
# build the disk with hdd/mkdisk.py, then run this.
#
#   powershell -File tools\test_diskboot.ps1 [-Disk ...] [-Keys "disk,help"]

param(
    [string]$Disk = "",
    [string[]]$Keys = @("disk"),
    [int]$SerialPort = 5571,
    [int]$MonitorPort = 4472,
    [int]$BootTimeoutSec = 25,
    [int]$WaitMs = 4000
)

# Start-Process builds a case-insensitive env dictionary; having both
# HTTPS_PROXY and https_proxy makes it throw.  Blank them instead.
$env:HTTPS_PROXY = ""; $env:https_proxy = ""
$env:HTTP_PROXY = "";  $env:http_proxy = ""

$ErrorActionPreference = "Stop"
$ws = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (!$Disk) { $Disk = Join-Path $ws "build\testdisk.raw" }
. (Join-Path $ws "tools-find.ps1")
$qemu = Get-Qemu
if (!(Test-Path $Disk)) { throw "disk not found: $Disk" }

$qemuArgs = @(
    "-drive", "file=$Disk,format=raw,if=ide,index=0,media=disk,cache=writeback",
    "-m", "64", "-display", "none", "-no-reboot", "-boot", "c",
    "-serial", "tcp:127.0.0.1:$SerialPort,server=on,wait=on",
    "-monitor", "tcp:127.0.0.1:$MonitorPort,server=on,nowait"
)
$p = Start-Process -FilePath $qemu -ArgumentList $qemuArgs -PassThru -WindowStyle Hidden
Start-Sleep -Milliseconds 400

$client = New-Object System.Net.Sockets.TcpClient
$deadline = (Get-Date).AddSeconds(12)
while ($true) {
    try { $client.Connect('127.0.0.1', $SerialPort); break }
    catch {
        if ((Get-Date) -gt $deadline) { throw "cannot connect serial: $($_.Exception.Message)" }
        Start-Sleep -Milliseconds 250
    }
}
$ns = $client.GetStream(); $ns.ReadTimeout = 500
$sb = New-Object System.Text.StringBuilder
$buf = New-Object byte[] 8192
function Pump([int]$ms) {
    $end = (Get-Date).AddMilliseconds($ms)
    while ((Get-Date) -lt $end) {
        try {
            if ($ns.DataAvailable) {
                $n = $ns.Read($buf, 0, $buf.Length)
                if ($n -gt 0) { [void]$sb.Append([Text.Encoding]::ASCII.GetString($buf, 0, $n)) }
            } else { Start-Sleep -Milliseconds 50 }
        } catch { Start-Sleep -Milliseconds 50 }
    }
}
function Wait-Text([string]$text, [int]$ms) {
    $end = (Get-Date).AddMilliseconds($ms)
    while ((Get-Date) -lt $end) {
        Pump 150
        if ($sb.ToString().Contains($text)) { return $true }
    }
    return $false
}

[void](Wait-Text "bsd45>" ($BootTimeoutSec * 1000))
foreach ($k in $Keys) {
    $b = [Text.Encoding]::ASCII.GetBytes($k + "`r")
    $ns.Write($b, 0, $b.Length); $ns.Flush()
    Pump 400; Start-Sleep -Milliseconds 300
}
Pump $WaitMs

try { $ns.Close(); $client.Close() } catch {}
if (-not $p.HasExited) { $p.Kill() }
Start-Sleep -Milliseconds 300
Write-Host $sb.ToString()
