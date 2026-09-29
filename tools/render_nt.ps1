param([string]$log = "$env:TEMP\iNES.log", [string]$out = "$env:TEMP\inesdbg\offline.bmp")

$pt = New-Object byte[] 4096
$nt = New-Object byte[] 960
$ptN = 0; $ntN = 0
$rePt = [regex]'(PT|NT)\s+([0-9A-Fa-f]{4})\s+([0-9A-Fa-f]{16})'
foreach($line in [System.IO.File]::ReadLines($log))
{
	$m = $rePt.Match($line)
	if(-not $m.Success) { continue }
	$off = [Convert]::ToInt32($m.Groups[2].Value, 16)
	$hex = $m.Groups[3].Value
	for($i = 0; $i -lt 8; $i++)
	{
		$v = [Convert]::ToByte($hex.Substring($i * 2, 2), 16)
		if($m.Groups[1].Value -eq 'PT') { if($off + $i -lt 4096) { $pt[$off + $i] = $v } }
		else { if($off + $i -lt 960) { $nt[$off + $i] = $v } }
	}
	if($m.Groups[1].Value -eq 'PT') { $ptN++ } else { $ntN++ }
}
Write-Host ("PT lines={0} NT lines={1}" -f $ptN, $ntN)
$hist = @{}
$max = 0
foreach($v in $nt) { $hist[$v] = ([int]$hist[$v]) + 1; if($v -gt $max) { $max = $v } }
Write-Host ("NT tile id max=0x{0:X2} distinct={1}" -f $max, $hist.Count)
Write-Host ("top tiles: " + (($hist.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 8 | ForEach-Object { "0x{0:X2}x{1}" -f $_.Key, $_.Value }) -join ' '))

$src = @'
using System; using System.IO;
public static class NtRender {
	public static void Run(string outPath, byte[] pt, byte[] nt) {
		using (var bmp = new BmpW(256, 240)) {
			for (int ty = 0; ty < 30; ty++) for (int tx = 0; tx < 32; tx++) {
				int id = nt[ty * 32 + tx]; int o = id * 16;
				for (int py = 0; py < 8; py++) {
					int lo = pt[o + py], hi = pt[o + py + 8];
					for (int px = 0; px < 8; px++) {
						int v = ((lo >> (7 - px)) & 1) | (((hi >> (7 - px)) & 1) << 1);
						int c = v == 0 ? 250 : (v == 1 ? 170 : (v == 2 ? 90 : 20));
						bmp.Set(tx * 8 + px, ty * 8 + py, c);
					}
				}
			}
			bmp.Save(outPath);
		}
	}
	class BmpW : IDisposable {
		byte[] px; int w, h;
		public BmpW(int w, int h) { this.w = w; this.h = h; px = new byte[w * h * 3]; }
		public void Set(int x, int y, int c) { int i = (y * w + x) * 3; px[i] = (byte)c; px[i+1] = (byte)c; px[i+2] = (byte)c; }
		public void Save(string path) {
			using (var fs = new FileStream(path, FileMode.Create)) using (var bw = new BinaryWriter(fs)) {
				int stride = (w * 3 + 3) / 4 * 4, dataSize = stride * h;
				bw.Write((byte)66); bw.Write((byte)77); bw.Write(54 + dataSize); bw.Write(0); bw.Write(54);
				bw.Write(40); bw.Write(w); bw.Write(h); bw.Write((short)1); bw.Write((short)24); bw.Write(0); bw.Write(dataSize); bw.Write(0); bw.Write(0); bw.Write(0); bw.Write(0);
				for (int y = h - 1; y >= 0; y--) { byte[] row = new byte[stride]; for (int x = 0; x < w; x++) { int i = (y * w + x) * 3; row[x*3] = px[i]; row[x*3+1] = px[i+1]; row[x*3+2] = px[i+2]; } bw.Write(row); }
			}
		}
		public void Dispose() {}
	}
}
'@
Add-Type -TypeDefinition $src -ErrorAction Stop
[NtRender]::Run($out, $pt, $nt)
Write-Host ("saved " + $out)
