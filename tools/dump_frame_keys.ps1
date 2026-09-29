# Headless frame dumper WITH key injection.
# Needed for menu / multicart mappers: the menu itself runs entirely in the powerup bank,
# so no bank register is ever written until you actually start a game.
# Must run under 32-bit PowerShell (inescore.dll is 32-bit):
#   C:\Windows\SysWOW64\WindowsPowerShell\v1.0\powershell.exe -ExecutionPolicy Bypass -File tools\dump_frame_keys.ps1 -rom <rom.nes> -keys DOWN,DOWN,START
#
# Key names: A B SELECT START UP DOWN LEFT RIGHT (bits defined in core/joypad.h)
param(
	[string]$rom,
	[string]$outDir = "$env:TEMP\inesdbg",
	[string]$dll = 'd:\proc\krh\iNES\bin\inescore.dll',
	[string[]]$keys = @('DOWN','DOWN','DOWN','DOWN','DOWN','DOWN','START'),
	[int]$holdMs = 150,
	[int]$gapMs = 120,
	[int]$bootMs = 2500,
	[int]$settleMs = 3000,
	[int]$shots = 2,
	[int]$logLevel = 10
)

$keyMap = @{
	A = 0x01; B = 0x02; SELECT = 0x04; START = 0x08
	UP = 0x10; DOWN = 0x20; LEFT = 0x40; RIGHT = 0x80
}

# Under `-File` invocation `-keys DOWN,START` arrives as ONE string, so split it here.
# Also accepts `-keys DOWN -keys START` and `-Command ... -keys @('DOWN','START')`.
$seq = @()
foreach($k in $keys)
{
	foreach($one in ([string]$k).Split(','))
	{
		$t = $one.Trim().ToUpper()
		if($t -ne '') { $seq += $t }
	}
}
if($seq.Count -eq 0) { Write-Host 'no key given, use -keys DOWN,START'; exit 1 }

$sw = [System.Diagnostics.Stopwatch]::StartNew()
if(-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }
Set-Location $outDir
[System.IO.Directory]::SetCurrentDirectory($outDir)
function T([string]$m) { Write-Host ('[' + $sw.Elapsed.TotalSeconds.ToString('F2') + 's] ' + $m) }

$romFile = Join-Path $outDir 'rom.nes'
if($rom) { Copy-Item -LiteralPath $rom -Destination $romFile -Force }
if(-not (Test-Path $romFile)) { Write-Host ('no rom.nes in ' + $outDir); exit 1 }

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
	[DllImport("__DLL__", CallingConvention = CallingConvention.Cdecl)]
	public static extern int ines_update_input(int main_key_state, int second_key_state, int reset_key_state);
}
'@
$src = $src -replace '__DLL__', $dll.Replace('\', '\\')
Add-Type -TypeDefinition $src -OutputAssembly $asm -OutputType Library -ReferencedAssemblies 'System.Drawing' -ErrorAction Stop
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
}

function Grab([string]$tag)
{
	$buf = New-Object byte[] 61440
	[NesCore]::ines_get_vedio_data($buf, 61440) | Out-Null
	$p = Join-Path $outDir ($tag + '.png')
	Save-Frame -buf $buf -path $p
	T ('saved ' + $tag + '.png')
}

function Press([string]$name)
{
	if(-not $keyMap.ContainsKey($name)) { Write-Host ('unknown key: ' + $name); return }
	$bits = $keyMap[$name]
	[NesCore]::ines_update_input($bits, 0, 0) | Out-Null
	Start-Sleep -Milliseconds $holdMs
	[NesCore]::ines_update_input(0, 0, 0) | Out-Null
	Start-Sleep -Milliseconds $gapMs
}

[NesCore]::ines_init_lib() | Out-Null
[NesCore]::ines_set_loglevel($logLevel, 0)
$r = [NesCore]::ines_start(1, $romFile, (Join-Path $outDir 'ram.sav'))
if($r -ne 0) { T ('ines_start failed: ' + $r); exit 1 }
T 'ines_start ok'

Start-Sleep -Milliseconds $bootMs
Grab 'menu'

foreach($k in $seq)
{
	Press $k
	T ('pressed ' + $k)
}
T ('key seq done: ' + ($seq -join ' '))

for($i = 1; $i -le $shots; $i++)
{
	Start-Sleep -Milliseconds $settleMs
	Grab ('shot' + $i)
}

T 'calling ines_stop ...'
[NesCore]::ines_stop() | Out-Null
T 'done'
