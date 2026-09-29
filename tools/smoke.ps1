# Headless smoke test: find a ROM by ASCII wildcard, run dump_frame.ps1 under 32-bit PowerShell.
param(
    [string]$pattern,
    [string]$tag
)

$root = 'D:\NES'
$hit = @(Get-ChildItem -LiteralPath $root -Filter $pattern -File -Recurse | Select-Object -First 1)
if ($hit.Count -eq 0) { Write-Host "no rom matched: $pattern"; exit 1 }

$outDir = Join-Path 'C:\Temp' $tag
if (-not (Test-Path -LiteralPath $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }
Copy-Item -LiteralPath $hit[0].FullName -Destination (Join-Path $outDir 'rom.nes') -Force

Write-Host ("rom  = " + $hit[0].Name)
Write-Host ("out  = " + $outDir)

$ps32 = 'C:\Windows\SysWOW64\WindowsPowerShell\v1.0\powershell.exe'
$dump = Join-Path $PSScriptRoot 'dump_frame.ps1'
& $ps32 -ExecutionPolicy Bypass -Command "& '$dump' -outDir '$outDir' -rom '$((Join-Path $outDir 'rom.nes'))' -waits @(1500,2500,4000)"
Write-Host "exit=$LASTEXITCODE"
