param([string]$log = "$env:TEMP\iNES.log", [string]$outDir = "$env:TEMP\inesdbg")

$romPath = Join-Path $outDir "rom.nes"
if(-not (Test-Path $romPath)) { Write-Host "ROM cache missing: $romPath"; exit 1 }
$rom = [System.IO.File]::ReadAllBytes($romPath)
$vo = 16 + 8 * 16384
$chrPages = [int](($rom.Length - $vo) / 1024)
Write-Host ("rom bytes = {0}, CHR 1K pages = {1}" -f $rom.Length, $chrPages)

$banks = New-Object System.Collections.Generic.List[byte[]]
for($k = 0; $k -lt 12; $k++) { $banks.Add((New-Object byte[] 1024)) }

$re = [regex]'MB\s+(\d+)\s+([0-9A-Fa-f]{4})\s+([0-9A-Fa-f]{16})'
$lines = 0
foreach($line in [System.IO.File]::ReadLines($log))
{
	$m = $re.Match($line)
	if(-not $m.Success) { continue }
	$b = [int]$m.Groups[1].Value
	if($b -lt 0 -or $b -gt 11) { continue }
	$off = [Convert]::ToInt32($m.Groups[2].Value, 16)
	$h = $m.Groups[3].Value
	for($i = 0; $i -lt 8; $i++) { $banks[$b][$off + $i] = [Convert]::ToByte($h.Substring($i * 2, 2), 16) }
	$lines++
}
Write-Host ("parsed MB lines = " + $lines)

for($b = 0; $b -lt 12; $b++)
{
	$nz = 0
	for($i = 0; $i -lt 1024; $i++) { if($banks[$b][$i] -ne 0) { $nz++ } }
	$hit = -1
	for($p = 0; $p -lt $chrPages -and $hit -lt 0; $p++)
	{
		$ok = $true
		for($i = 0; $i -lt 1024; $i++)
		{
			if($rom[$vo + $p * 1024 + $i] -ne $banks[$b][$i]) { $ok = $false; break }
		}
		if($ok) { $hit = $p }
	}
	$desc = if($hit -ge 0) { ("VROM 1K page {0} (addr 0x{1:X5})" -f $hit, ($hit * 1024)) } else { "(not a VROM page -> CIRAM/other)" }
	Write-Host ("bank[{0,2}] nonzero={1,4}  -> {2}" -f $b, $nz, $desc)
}

for($b = 8; $b -lt 12; $b++)
{
	$nz = 0
	for($i = 0; $i -lt 960; $i++) { if($banks[$b][$i] -ne 0) { $nz++ } }
	Write-Host ("nt window bank[{0}]: nonzero(960B)={1}" -f $b, $nz)
}
