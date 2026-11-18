# test_hdd_install.ps1 - end-to-end QEMU test of the ISO -> virtual disk install.
#
# 1. create a blank 64 MiB raw disk (QEMU presents it as an ATA disk)
# 2. boot the live ISO with the disk attached, run `install` in the monitor
# 3. reboot with NO cdrom, boot from the disk, check FFS root + shell prompt

param(
    [string]$Iso = "",
    [string]$Disk = "",
    [int]$DiskMB = 64,
    [int]$SerialPort = 5561,
    [int]$MonitorPort = 4461,
    [int]$BootTimeoutSec = 40,
    [int]$InstallTimeoutSec = 90
)

$ErrorActionPreference = "Stop"
$ws = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (!$Iso)  { $Iso  = Join-Path $ws "out\BSD45-ELF-x86.iso" }
if (!$Disk) { $Disk = Join-Path $ws "build\testdisk.raw" }
. (Join-Path $ws "tools-find.ps1")
$qemu = Get-Qemu
if (!$qemu) { throw "qemu-system-i386.exe not found" }
if (!(Test-Path $Iso)) { throw "ISO not found: $Iso" }

New-Item -ItemType Directory -Force -Path (Split-Path $Disk -Parent) | Out-Null
if (Test-Path $Disk) { Remove-Item $Disk -Force }

function New-BlankDisk([string]$path, [int]$mb) {
    $fs = [IO.File]::Create($path)
    $fs.SetLength([long]$mb * 1024 * 1024)
    $fs.Close()
    Write-Host "created disk $path ($mb MiB)"
}
New-BlankDisk $Disk $DiskMB

function Run-Phase([string[]]$qemuArgs, [string[]]$keys, [string]$waitText,
                   [int]$waitMs) {
    $p = Start-Process -FilePath $qemu -ArgumentList $qemuArgs -PassThru -WindowStyle Hidden
    Start-Sleep -Milliseconds 600
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
    try {
        [void](Wait-Text "bsd45>" ($BootTimeoutSec * 1000))
        foreach ($k in $keys) {
            $bytes = [Text.Encoding]::ASCII.GetBytes($k + "`r")
            $ns.Write($bytes, 0, $bytes.Length); $ns.Flush()
            Pump 300; Start-Sleep -Milliseconds 400
        }
        if ($waitText) { [void](Wait-Text $waitText $waitMs) }
        else { Pump 2500 }
        Pump 300
    } finally {
        try { $ns.Close(); $client.Close() } catch {}
        if (-not $p.HasExited) { $p.Kill() }
        Start-Sleep -Milliseconds 400
    }
    return $sb.ToString()
}

# QEMU rejects `index=` combined with `bus=`.  Use the canonical
# if=ide,index=N form: index 0 = primary master (disk, the VMware ide0:0
# equivalent), index 2 = secondary master (CD-ROM, the VMware ide1:0 one).
$diskArg = $Disk

# ---- phase 1: boot ISO, run install ------------------------------------
Write-Host "`n=== PHASE 1: boot ISO + install ==="
$cdArg = "file=$Iso,media=cdrom,if=ide,index=2"
$args1 = @(
    "-drive", $cdArg, "-boot", "d", "-m", "64",
    "-drive", "file=$diskArg,format=raw,if=ide,index=0,media=disk,cache=writeback",
    "-display", "none", "-no-reboot",
    "-serial", "tcp:127.0.0.1:$SerialPort,server=on,wait=on",
    "-monitor", "tcp:127.0.0.1:$MonitorPort,server=on,nowait"
)
$log1 = Run-Phase $args1 @("disk", "install") "install: SUCCESS" ($InstallTimeoutSec * 1000)
Set-Content (Join-Path $ws "build\qemu-install.log") $log1 -Encoding UTF8
Write-Host $log1

if (-not $log1.Contains("install: SUCCESS")) {
    Write-Host "`n*** INSTALL FAILED ***" -ForegroundColor Red
    exit 1
}
Write-Host "`n*** INSTALL OK ***" -ForegroundColor Green

# ---- phase 2: boot from disk, no CD ------------------------------------
Write-Host "`n=== PHASE 2: boot from virtual disk (no CD) ==="
$args2 = @(
    "-drive", "file=$diskArg,format=raw,if=ide,index=0,media=disk,cache=writeback",
    "-m", "64", "-display", "none", "-no-reboot", "-boot", "c",
    "-serial", "tcp:127.0.0.1:$SerialPort,server=on,wait=on",
    "-monitor", "tcp:127.0.0.1:$MonitorPort,server=on,nowait"
)
$log2 = Run-Phase $args2 @("disk", "help") "vstat" 8000
Set-Content (Join-Path $ws "build\qemu-diskboot.log") $log2 -Encoding UTF8
Write-Host $log2

$fsOk = $log2.Contains("FFS mounted") -or $log2.Contains("JLFS mounted")
$ok = $fsOk -and $log2.Contains("root: FFS on ATA disk")
if ($ok) {
    Write-Host "`n*** DISK BOOT OK (FFS/JLFS root mounted) ***" -ForegroundColor Green
    exit 0
} else {
    Write-Host "`n*** DISK BOOT FAILED ***" -ForegroundColor Red
    exit 1
}
