# =====================================================================
# iNES 应用图标打包脚本 (Windows)
#
# 作用: mac/icon/iNES-icon.svg -> win32/iNES.ico + win32/small.ico
#       加 -UpdateIcns 时同时重打包 mac/icon/iNES.icns, 三边共用同一张 SVG。
#
# 依赖: Windows PowerShell 5.1(或 Windows 上的 pwsh) + .NET 的 System.Drawing;
#       渲染 SVG 需要 Chrome / Edge(无头模式)。两者都没有时退化为
#       "用 .icns 里已有的位图 + GDI+ 缩放"(见下方"回退"一节)。
#
# 用法: powershell -NoProfile -ExecutionPolicy Bypass -File mac/icon/make-ico.ps1
#       powershell ... -File mac/icon/make-ico.ps1 -UpdateIcns
#
# 为什么不用 .icns 里的位图当主源:
#       make-icns.sh 用 qlmanage 光栅化 SVG, 而 qlmanage 会垫一层白底 ——
#       实测原 .icns 的四角是 A=255 的纯白, 图标变成"白方块"。SVG 本身是
#       透明边距(macOS Big Sur 图标网格: 1024 画布 + 824 主体), 所以这里
#       改用无头浏览器渲染: --default-background-color=00000000 可取到真透明,
#       走 Blink 的矢量渲染, 并且每个尺寸都按目标像素直接渲染(而不是
#       大图缩下来), 小尺寸下的十字键/圆钮边缘明显更干净。
#
# .ico 用 32bpp DIB(BGRA, 自下而上)而不是 PNG 条目:
#       rc.exe 与各版本 Windows 的资源加载器对 DIB 支持最广, PNG 条目要 Vista 起才认。
#
# 注意: 本文件带 UTF-8 BOM —— Windows PowerShell 5.1 默认按本地代码页读 .ps1,
#       没有 BOM 中文注释会乱码(不影响逻辑, 但会污染输出)。
# =====================================================================
param(
    [string]  $Svg    = (Join-Path $PSScriptRoot 'iNES-icon.svg'),
    [string]  $Icns   = (Join-Path $PSScriptRoot 'iNES.icns'),
    # 本脚本在 mac/icon/ 下: 上两级才是仓库根, 其下的 win32/ 才是图标的去处
    [string]  $OutDir = (Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) 'win32'),
    [switch]  $UpdateIcns
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

$SvgSize = 1024        # SVG 画布边长(argv 与 icns 都按这个基准)

# 主图标尺寸(资源管理器/Alt-Tab/任务栏各档位)与小图标尺寸(标题栏)
$MainIcoSizes = @(16, 24, 32, 40, 48, 64, 96, 128, 256)
$SmallIcoSizes = @(16, 20, 24, 32, 48)

# icns 类型 -> 像素边长(只收录 iconutil 写出的 PNG 条目)
$icnsTypeSizes = @{
    'ic10' = 1024; 'ic09' = 512; 'ic14' = 512;
    'ic08' = 256;  'ic13' = 256;
    'ic07' = 128;
    'ic12' = 64;
    'ic11' = 32
}

# ---------------------------------------------------------------------
# 0. 小工具
# ---------------------------------------------------------------------
function Write-BeUInt32([System.IO.BinaryWriter] $bw, [uint32] $v) {
    $bytes = [BitConverter]::GetBytes($v)
    [array]::Reverse($bytes)
    $bw.Write($bytes)
}

function Find-Browser {
    $candidates = @(
        (Join-Path $env:ProgramFiles 'Google\Chrome\Application\chrome.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Google\Chrome\Application\chrome.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft\Edge\Application\msedge.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft\Edge\Application\msedge.exe')
    )
    foreach ($c in $candidates) { if ($c -and (Test-Path $c)) { return $c } }
    return $null
}

# ---------------------------------------------------------------------
# 1. SVG -> 指定像素边长的透明底 PNG(主源)
# ---------------------------------------------------------------------
function Invoke-SvgRender([string] $browser, [string] $svg, [string] $png, [int] $n) {
    $url = 'file:///' + ($svg -replace '\\', '/')
    $scale = ([double] $n / $SvgSize).ToString([System.Globalization.CultureInfo]::InvariantCulture)
    $cliArgs = @(
        '--headless=new',
        '--disable-gpu',
        '--hide-scrollbars',
        "--force-device-scale-factor=$scale",     # 1024 视口 x 缩放 = 目标像素
        "--window-size=$SvgSize,$SvgSize",
        '--default-background-color=00000000',    # 关键: 透明底, 否则是白底
        '--virtual-time-budget=3000',
        "--screenshot=$png",
        $url
    )
    $null = Start-Process -FilePath $browser -ArgumentList $cliArgs -Wait -PassThru -WindowStyle Hidden
    Start-Sleep -Milliseconds 300
    # 无头截图即使失败也可能返回 0, 因此以产物 + 左上角透明度为准
    if (-not (Test-Path $png)) { throw "浏览器未产出截图: $png" }

    $bmp = [System.Drawing.Bitmap]::FromFile($png)
    try {
        if ($bmp.Width -ne $n -or $bmp.Height -ne $n) {
            throw ('渲染尺寸不符: 期望 ' + $n + 'x' + $n + ' 实际 ' + $bmp.Width + 'x' + $bmp.Height)
        }
        $px = $bmp.GetPixel(0, 0)
        if ($px.A -gt 8) { throw ('渲染结果没有透明边距(A=' + $px.A + ')') }
    } catch {
        $bmp.Dispose()
        throw
    }
    $bmp.Dispose()
    return $png
}

# ---------------------------------------------------------------------
# 2a. 拆分 .icns, 取出各尺寸 PNG(无浏览器时的回退)
# ---------------------------------------------------------------------
function Split-Icns([string] $path, [string] $tmpDir) {
    if (-not (Test-Path $path)) { throw "找不到源图标 $path" }

    $result = @{}                       # 像素边长 -> PNG 文件完整路径
    $fs = [System.IO.File]::OpenRead($path)
    $br = New-Object System.IO.BinaryReader($fs)
    try {
        $magic = [System.Text.Encoding]::ASCII.GetString($br.ReadBytes(4))
        $null  = $br.ReadBytes(4)       # 文件总长度(大端)
        if ($magic -ne 'icns') { throw "$path 不是 icns 文件(magic=$magic)" }

        while ($fs.Position -lt $fs.Length) {
            $type = [System.Text.Encoding]::ASCII.GetString($br.ReadBytes(4))
            $lenBytes = $br.ReadBytes(4)
            [array]::Reverse($lenBytes)
            $len = [BitConverter]::ToUInt32($lenBytes, 0)
            if ($len -lt 8) { throw "icns 条目 $type 长度非法: $len" }

            $payloadLen = $len - 8
            $start = $fs.Position
            $isPng = $false
            if ($payloadLen -ge 8) {
                $head = $br.ReadBytes(8)
                $isPng = ($head[0] -eq 0x89 -and $head[1] -eq 0x50 -and
                          $head[2] -eq 0x4E -and $head[3] -eq 0x47)
            }

            if ($isPng -and $icnsTypeSizes.ContainsKey($type)) {
                $size = [int] $icnsTypeSizes[$type]
                $fs.Position = $start
                $png = $br.ReadBytes($payloadLen)
                # 同尺寸(1x/2x)只保留第一份
                if (-not $result.ContainsKey($size)) {
                    $file = Join-Path $tmpDir "icon_$size.png"
                    [System.IO.File]::WriteAllBytes($file, $png)
                    $result[$size] = $file
                }
            }
            $fs.Position = $start + $payloadLen
        }
    } finally {
        $br.Close(); $fs.Dispose()
    }

    if ($result.Count -eq 0) { throw "$path 中没有可用的 PNG 图标条目" }
    return $result
}

# ---------------------------------------------------------------------
# 2b. GDI+ 缩放(回退路径 / 补浏览器没渲染的尺寸)
#     逐档折半(1024->512->...->16)比一步缩到 16 少很多锯齿
# ---------------------------------------------------------------------
function New-Resized([System.Drawing.Bitmap] $src, [int] $n, $created) {
    $bmp = New-Object System.Drawing.Bitmap -ArgumentList @($n, $n)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try {
        $g.InterpolationMode  = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.SmoothingMode      = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $g.PixelOffsetMode    = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
        $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
        $g.DrawImage($src, 0, 0, $n, $n)
    } finally {
        $g.Dispose()
    }
    $created.Add($bmp) | Out-Null
    return $bmp
}

function Get-Size([int] $n, $map, $created) {
    if ($map.ContainsKey($n)) { return $map[$n] }

    $sorted = @($map.Keys | Sort-Object)
    $srcN = 0
    foreach ($s in $sorted) { if ($s -ge $n) { $srcN = $s; break } }
    if ($srcN -eq 0) { $srcN = $sorted[$sorted.Count - 1] }

    Write-Host ("  缩放 {0}x{0} <- {1}x{1}" -f $n, $srcN)
    $bmp = New-Resized $map[$srcN] $n $created
    $map[$n] = $bmp
    return $bmp
}

# ---------------------------------------------------------------------
# 3. 位图 -> 32bpp BGRA DIB(自下而上) + 全 0 的 AND 掩码
#    alpha 由 XOR 的 A 通道表达, 掩码留 0 即可(与主流图标生成器的做法一致)
# ---------------------------------------------------------------------
function ConvertTo-Dib([System.Drawing.Bitmap] $bmp) {
    $w = $bmp.Width
    $h = $bmp.Height
    $rect = New-Object System.Drawing.Rectangle -ArgumentList @(0, 0, $w, $h)
    $data = $bmp.LockBits($rect,
        [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

    try {
        $stride = [Math]::Abs($data.Stride)
        $xor = New-Object byte[] ($w * $h * 4)
        $dst = 0
        for ($y = 0; $y -lt $h; $y++) {
            # DIB 自下而上; LockBits 的行序由 Stride 的正负决定
            $srcY = $h - 1 - $y
            if ($data.Stride -lt 0) { $srcY = $y }
            $p = [IntPtr]::Add($data.Scan0, $srcY * $stride)
            [System.Runtime.InteropServices.Marshal]::Copy($p, $xor, $dst, $w * 4)
            $dst += $w * 4
        }
    } finally {
        $bmp.UnlockBits($data)
    }

    $maskRowBytes = [int] ([Math]::Ceiling($w / 32.0) * 4)
    $mask = New-Object byte[] ($maskRowBytes * $h)

    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter($ms)
    try {
        $bw.Write([int] 40)                 # biSize
        $bw.Write([int] $w)                 # biWidth
        $bw.Write([int] ($h * 2))           # biHeight: XOR + AND
        $bw.Write([uint16] 1)               # biPlanes
        $bw.Write([uint16] 32)              # biBitCount
        $bw.Write([uint32] 0)               # biCompression = BI_RGB
        $bw.Write([uint32] ($xor.Length + $mask.Length))  # biSizeImage
        $bw.Write([int] 0); $bw.Write([int] 0); $bw.Write([int] 0); $bw.Write([int] 0)
        $bw.Write($xor)
        $bw.Write($mask)
        $bw.Flush()
        return $ms.ToArray()
    } finally {
        $bw.Dispose(); $ms.Dispose()
    }
}

# ---------------------------------------------------------------------
# 4. 写 .ico
# ---------------------------------------------------------------------
function Write-Ico([string] $path, [int[]] $sizes, $map, $created) {
    $entries = New-Object System.Collections.ArrayList
    foreach ($n in $sizes) {
        $bmp = Get-Size $n $map $created
        $dib = ConvertTo-Dib $bmp
        $entries.Add(@{ Size = $n; Data = $dib }) | Out-Null
    }

    $count = $entries.Count
    $headerLen = 6 + 16 * $count
    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter($ms)
    try {
        $bw.Write([uint16] 0)               # reserved
        $bw.Write([uint16] 1)               # type = icon
        $bw.Write([uint16] $count)

        $offset = $headerLen
        foreach ($e in $entries) {
            $n = [int] $e.Size
            $dim = $n
            if ($dim -ge 256) { $dim = 0 }  # 256 在单字节字段里记为 0
            $bw.Write([byte] $dim)          # width
            $bw.Write([byte] $dim)          # height
            $bw.Write([byte] 0)             # 颜色数(>=8bpp 时填 0)
            $bw.Write([byte] 0)             # reserved
            $bw.Write([uint16] 1)           # planes
            $bw.Write([uint16] 32)          # bitCount
            $bw.Write([uint32] $e.Data.Length)
            $bw.Write([uint32] $offset)
            $offset += $e.Data.Length
        }
        foreach ($e in $entries) {
            # 显式转成 byte[] 并走 Write(byte[], int, int) 重载:
            # 直接 Write($e.Data) 时 PowerShell 可能把它当 object 处理, 只落 1 个字节
            $bytes = [byte[]] $e.Data
            $bw.Write($bytes, 0, $bytes.Length)
        }
        $bw.Flush()
        [System.IO.File]::WriteAllBytes($path, $ms.ToArray())
    } finally {
        $bw.Dispose(); $ms.Dispose()
    }

    $kb = [Math]::Round((Get-Item $path).Length / 1KB, 1)
    Write-Host ("写入 " + $path + " (" + $count + " 个尺寸: " + (($sizes | Sort-Object) -join '/') + "; " + $kb + " KB)")
}

# ---------------------------------------------------------------------
# 5. 写 .icns: 与 iconutil 产出的类型集合保持一致, 条目一律用 PNG
# ---------------------------------------------------------------------
function Write-Icns([string] $path, $map, $created, [string] $tmpDir) {
    $plan = @(
        @{ Type = 'ic12'; Size = 64   },   # 32x32@2x
        @{ Type = 'ic07'; Size = 128  },   # 128x128
        @{ Type = 'ic13'; Size = 256  },   # 128x128@2x
        @{ Type = 'ic08'; Size = 256  },   # 256x256
        @{ Type = 'ic14'; Size = 512  },   # 256x256@2x
        @{ Type = 'ic09'; Size = 512  },   # 512x512
        @{ Type = 'ic10'; Size = 1024 },   # 512x512@2x
        @{ Type = 'ic11'; Size = 32   }    # 16x16@2x
    )

    $items = New-Object System.Collections.ArrayList
    $total = 8
    foreach ($p in $plan) {
        $n = [int] $p.Size
        $bmp = Get-Size $n $map $created
        $pngFile = Join-Path $tmpDir ('icns_' + $p.Type + '_' + $n + '.png')
        $bmp.Save($pngFile, [System.Drawing.Imaging.ImageFormat]::Png)
        $bytes = [System.IO.File]::ReadAllBytes($pngFile)
        $items.Add(@{ Type = [string] $p.Type; Data = $bytes }) | Out-Null
        $total += 8 + $bytes.Length
    }

    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter($ms)
    try {
        $bw.Write([System.Text.Encoding]::ASCII.GetBytes('icns'))
        Write-BeUInt32 $bw ([uint32] $total)
        foreach ($it in $items) {
            $bw.Write([System.Text.Encoding]::ASCII.GetBytes($it.Type))
            Write-BeUInt32 $bw ([uint32] (8 + $it.Data.Length))
            $bw.Write([byte[]] $it.Data)
        }
        $bw.Flush()
        [System.IO.File]::WriteAllBytes($path, $ms.ToArray())
    } finally {
        $bw.Dispose(); $ms.Dispose()
    }

    $kb = [Math]::Round((Get-Item $path).Length / 1KB, 1)
    Write-Host ("写入 " + $path + " (" + $items.Count + " 个 PNG 条目; " + $kb + " KB)")
}

# ---------------------------------------------------------------------
# main
# ---------------------------------------------------------------------
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('ines-icon-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmp | Out-Null

$map     = @{}                                  # 像素边长 -> Bitmap
$created = New-Object System.Collections.ArrayList
$loaded  = New-Object System.Collections.ArrayList

try {
    # 需要哪些尺寸
    $sizes = New-Object System.Collections.Generic.List[int]
    foreach ($n in $MainIcoSizes) { if (-not $sizes.Contains($n)) { $sizes.Add($n) } }
    foreach ($n in $SmallIcoSizes) { if (-not $sizes.Contains($n)) { $sizes.Add($n) } }
    if ($UpdateIcns) {
        foreach ($n in 512, 1024) { if (-not $sizes.Contains($n)) { $sizes.Add($n) } }
    }

    $browser = Find-Browser
    if ($browser -and (Test-Path $Svg)) {
        Write-Host ('主源: 无头浏览器矢量渲染 ' + (Split-Path $Svg -Leaf))
        foreach ($n in $sizes) {
            $png = Join-Path $tmp "render-$n.png"
            $null = Invoke-SvgRender $browser $Svg $png $n
            $bmp = [System.Drawing.Bitmap]::FromFile($png)
            $loaded.Add($bmp) | Out-Null
            $map[$n] = $bmp
            Write-Host ('  渲染 ' + $n + 'x' + $n)
        }
    } else {
        Write-Host ('主源: ' + (Split-Path $Icns -Leaf) + ' (未找到 Chrome/Edge, 退化为 GDI+ 缩放)')
        $pngFiles = Split-Icns $Icns $tmp
        $avail = @($pngFiles.Keys | Sort-Object)
        Write-Host ('  可用尺寸: ' + ($avail -join '/'))
        foreach ($s in $avail) {
            $b = [System.Drawing.Bitmap]::FromFile($pngFiles[$s])
            $loaded.Add($b) | Out-Null
            $map[$s] = $b
        }
        # 逐档折半: 让小尺寸也有干净的边缘
        $biggest = $avail[$avail.Count - 1]
        foreach ($n in 512, 256, 128, 64, 32, 16) {
            if ((-not $map.ContainsKey($n)) -and ($biggest -gt $n) -and $map.ContainsKey($n * 2)) {
                $map[$n] = New-Resized $map[$n * 2] $n $created
            }
        }
    }

    if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }

    # 主图标: 资源管理器/Alt-Tab/任务栏各档位
    Write-Ico (Join-Path $OutDir 'iNES.ico') $MainIcoSizes $map $created

    # 小图标: 标题栏与窗口角标
    Write-Ico (Join-Path $OutDir 'small.ico') $SmallIcoSizes $map $created

    if ($UpdateIcns) { Write-Icns $Icns $map $created $tmp }

    Write-Host '完成: iNES.ico / small.ico 已与 mac/icon/iNES-icon.svg 同步'
} finally {
    foreach ($b in $created) { if ($b) { $b.Dispose() } }
    foreach ($b in $loaded)  { if ($b) { $b.Dispose() } }
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
}
