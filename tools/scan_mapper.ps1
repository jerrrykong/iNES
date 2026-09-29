$root = 'D:\NES'
$ids = @(225, 255)

Get-ChildItem -LiteralPath $root -Filter *.nes -File -Recurse | ForEach-Object {
    $fs = [System.IO.File]::OpenRead($_.FullName)
    $hdr = New-Object byte[] 16
    $fs.Read($hdr, 0, 16) | Out-Null
    $fs.Close()

    if ($hdr[0] -ne 0x4E -or $hdr[1] -ne 0x45 -or $hdr[2] -ne 0x53) { return }

    $mapper = [int](([int]($hdr[6] -shr 4)) -bor ([int]($hdr[7] -band 0xF0)))
    if (-not ($ids -contains $mapper)) { return }

    Write-Host ("mapper={0}`tPRG={1}`tCHR={2}`tflags6=0x{3:X2}`t{4}" -f $mapper, $hdr[4], $hdr[5], $hdr[6], $_.Name)
}
