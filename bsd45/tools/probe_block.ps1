# probe_block.ps1 - ask the QEMU monitor how the test disk was opened (ro flag)
# and capture QEMU's own stderr, to explain ATA WRITE -> ABRT (err=0x04).

# Start-Process builds a case-insensitive env dictionary; having both
# HTTPS_PROXY and https_proxy makes it throw.  Blank them instead.
$env:HTTPS_PROXY = ""; $env:https_proxy = ""
$env:HTTP_PROXY = "";  $env:http_proxy = ""

$ws = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$iso = Join-Path $ws "out\BSD45-ELF-x86.iso"
$disk = Join-Path $ws "build\testdisk.raw"
. (Join-Path $ws "tools-find.ps1")
$qemu = Get-Qemu

if (Test-Path $disk) { Remove-Item $disk -Force }
$fs = [IO.File]::Create($disk); $fs.SetLength(64MB); $fs.Close()

$errFile = Join-Path $ws "build\qemu-stderr.txt"
$outFile = Join-Path $ws "build\qemu-stdout.txt"
$monPort = 4477

$args = @(
    "-drive", "file=$iso,media=cdrom,if=ide,index=2",
    "-drive", "file=$disk,format=raw,if=ide,index=0,media=disk,cache=writeback",
    "-boot", "d", "-m", "64", "-display", "none", "-no-reboot",
    "-serial", "null",
    "-monitor", "tcp:127.0.0.1:$monPort,server=on,nowait"
)
Write-Host "QEMU args: $($args -join ' ')"
$p = Start-Process -FilePath $qemu -ArgumentList $args -PassThru -WindowStyle Hidden `
     -RedirectStandardError $errFile -RedirectStandardOutput $outFile

Start-Sleep -Seconds 2
$client = New-Object System.Net.Sockets.TcpClient
$client.Connect('127.0.0.1', $monPort)
$ns = $client.GetStream(); $ns.ReadTimeout = 800
function Drain([int]$ms) {
    $end = (Get-Date).AddMilliseconds($ms); $sb = New-Object Text.StringBuilder
    $buf = New-Object byte[] 65536
    while ((Get-Date) -lt $end) {
        try {
            if ($ns.DataAvailable) {
                $n = $ns.Read($buf, 0, $buf.Length)
                if ($n -gt 0) { [void]$sb.Append([Text.Encoding]::ASCII.GetString($buf, 0, $n)) }
            } else { Start-Sleep -Milliseconds 40 }
        } catch { Start-Sleep -Milliseconds 40 }
    }
    return $sb.ToString()
}
[void](Drain 800)
foreach ($cmd in @("info block", "info blockstats")) {
    $b = [Text.Encoding]::ASCII.GetBytes("$cmd`r")
    $ns.Write($b, 0, $b.Length); $ns.Flush()
    Write-Host "`n--- $cmd ---"
    Write-Host (Drain 1200)
}
try { $ns.Close(); $client.Close() } catch {}
if (-not $p.HasExited) { $p.Kill() }
Start-Sleep -Milliseconds 500

Write-Host "`n--- QEMU stderr ---"
if (Test-Path $errFile) { Get-Content $errFile } else { "(none)" }
