param([int]$offset = 0x38000, [string]$out = "$env:TEMP\inesdbg\F_chr.bmp")

$romPath = Join-Path "$env:TEMP\inesdbg" "rom.nes"
$rom = [System.IO.File]::ReadAllBytes($romPath)
$vo = 16 + 8 * 16384

$src = @'
using System; using System.IO;
public static class ChrView4 {
	public static void Run(byte[] rom, int vromOff, int startByte, int len, int cols, string outPath) {
		int tileCount = len / 16; int rows = (tileCount + cols - 1) / cols;
		using (var bmp = new BmpW(cols * 8, rows * 8)) {
			for (int t = 0; t < tileCount; t++) {
				int o = vromOff + startByte + t * 16; int tx = (t % cols) * 8, ty = (t / cols) * 8;
				for (int py = 0; py < 8; py++) {
					int lo = rom[o + py], hi = rom[o + py + 8];
					for (int px = 0; px < 8; px++) {
						int v = ((lo >> (7 - px)) & 1) | (((hi >> (7 - px)) & 1) << 1);
						int c = v == 0 ? 250 : (v == 1 ? 170 : (v == 2 ? 90 : 20));
						bmp.Set(tx + px, ty + py, c);
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
[ChrView4]::Run($rom, $vo, $offset, 4096, 16, $out)
Write-Host ("saved " + $out + " (CHR 0x" + $offset.ToString("X5") + ", 4KB)")
