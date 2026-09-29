# Local frame dumper: drives inescore.dll directly and saves frames as PNG.
# v2: step timing, cached C# assembly, optional skip of ines_stop().
param(
	[string]$outDir,
	[string]$dll,
	[string]$rom,
	[int[]]$waits,
	[switch]$noStop,
	[int]$logLevel = 10,
	[int]$cpuTrace = 0
)

if(-not $outDir) { $outDir = $env:TEMP + '\inesdbg' }
if(-not $dll)    { $dll = 'd:\proc\krh\iNES\bin\inescore.dll' }
if(-not $waits -or $waits.Count -eq 0) { $waits = @(1200, 2500, 4000, 6000) }

$sw = [System.Diagnostics.Stopwatch]::StartNew()
$logFile = Join-Path $outDir 'dump.log'
if(-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }
Set-Location $outDir
[System.IO.Directory]::SetCurrentDirectory($outDir)
[System.IO.File]::WriteAllText($logFile, ('pid=' + $PID + ' start=' + (Get-Date).ToString('HH:mm:ss') + "`r`n"), (New-Object System.Text.UTF8Encoding($false)))
function T([string]$m)
{
	$line = '[' + $sw.Elapsed.TotalSeconds.ToString('F2') + 's] ' + $m
	Write-Host $line
	[System.IO.File]::AppendAllText($logFile, $line + "`r`n")
}
$romFile = Join-Path $outDir 'rom.nes'
$ramFile = Join-Path $outDir 'ram.sav'
if($rom) { Copy-Item $rom $romFile -Force }
if(-not (Test-Path $romFile)) { Write-Host 'no rom.nes in outDir'; exit 1 }

T ('dll  = ' + $dll)
T ('rom  = ' + $romFile)

$asm = Join-Path $outDir 'NesCore.dll'
$src = @'
using System;
using System.Runtime.InteropServices;
public class NesCore {
	[DllImport("__DLL__", CallingConvention = CallingConvention.Cdecl)]
	public static extern int ines_init_lib();
	[DllImport("__DLL__", CallingConvention = CallingConvention.Cdecl)]
	public static extern void ines_set_loglevel(int level, int cpu_trace);
	[DllImport("__DLL__", CallingConvention = CallingConvention.Cdecl)]
	public static extern int ines_start(int is_ntsc, string rom_file, string ram_file);
	[DllImport("__DLL__", CallingConvention = CallingConvention.Cdecl)]
	public static extern int ines_stop();
	[DllImport("__DLL__", CallingConvention = CallingConvention.Cdecl)]
	public static extern int ines_get_vedio_data(byte[] buffer, int len);
}
'@
$src = $src -replace '__DLL__', $dll.Replace('\', '\\')

T 'compiling / loading C# glue ...'
Add-Type -TypeDefinition $src -OutputAssembly $asm -OutputType Library -ReferencedAssemblies 'System.Drawing' -ErrorAction Stop
T ('assembly cached: ' + $asm)
Add-Type -Path $asm -ErrorAction Stop
T 'glue loaded'

$palHex = '7F7F7F,2000B0,2800B8,6010A0,982078,B01030,A03000,784000,485800,386800,386C00,306040,305080,000000,000000,000000,BCBCBC,4060F8,4040FF,9040F0,D840C0,D84060,E05000,C07000,888800,50A000,48A810,48A068,4090C0,000000,000000,000000,FFFFFF,60A0FF,5080FF,A070FF,F060FF,FF60B0,FF7830,FFA000,E8D020,98E800,70F040,70E090,60D0E0,606060,000000,000000,FFFFFF,90D0FF,A0B8FF,C0B0FF,E0B0FF,FFB8E8,FFC8B8,FFD8A0,FFF090,C8F080,A0F0A0,A0FFC8,A0FFF0,A0A0A0,000000,000000'
$pal64 = @()
foreach($s in $palHex.Split(',')) { $pal64 += [Convert]::ToInt32($s, 16) }

function Save-Frame
{
	param([byte[]]$buf, [string]$path)

	$bmp = New-Object System.Drawing.Bitmap(256, 240, [System.Drawing.Imaging.PixelFormat]::Format8bppIndexed)
	$pal = $bmp.Palette
	for($i = 0; $i -lt 64; $i++)
	{
		$c = $pal64[$i]
		$pal.Entries[$i] = [System.Drawing.Color]::FromArgb((($c -shr 16) -band 0xFF), (($c -shr 8) -band 0xFF), ($c -band 0xFF))
	}
	$bmp.Palette = $pal
	$rect = New-Object System.Drawing.Rectangle(0, 0, 256, 240)
	$bd = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $bmp.PixelFormat)
	for($y = 0; $y -lt 240; $y++)
	{
		[System.Runtime.InteropServices.Marshal]::Copy($buf, (239 - $y) * 256, [IntPtr]($bd.Scan0.ToInt64() + $y * $bd.Stride), 256)
	}
	$bmp.UnlockBits($bd)
	$bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
	$bmp.Dispose()
	[System.IO.File]::WriteAllBytes(($path -replace '\.png$', '.bin'), $buf)
	T ('saved: ' + $path)
}

[NesCore]::ines_init_lib() | Out-Null
T 'ines_init_lib ok'
[NesCore]::ines_set_loglevel($logLevel, $cpuTrace)
$r = [NesCore]::ines_start(1, $romFile, $ramFile)
if($r -ne 0)
{
	T ('ines_start failed: ' + $r)
	exit 1
}
T 'ines_start ok (thread running)'

$buf = New-Object byte[] 61440
$idx = 0
foreach($w in $waits)
{
	Start-Sleep -Milliseconds $w
	[NesCore]::ines_get_vedio_data($buf, 61440) | Out-Null
	$idx = $idx + 1
	Save-Frame -buf $buf -path (Join-Path $outDir ('frame_' + $idx + '.png'))
}

if($noStop)
{
	T 'skip ines_stop (process exit will kill the thread)'
	exit 0
}

T 'calling ines_stop ...'
[NesCore]::ines_stop() | Out-Null
T 'done'
