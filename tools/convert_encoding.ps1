<#
.SYNOPSIS
    Convert repository sources to UTF-8 (no BOM) + LF line endings.
.DESCRIPTION
    Per-file flow:
      1. read raw bytes, strip UTF-8 BOM if present
      2. decode: pure ASCII -> ASCII; else strict UTF-8; else GBK (code page 936)
      3. normalize CRLF/CR to LF
      4. re-encode as UTF-8 without BOM, write only if bytes actually changed
.NOTES
    Kept ASCII-only on purpose: PowerShell 5.1 reads BOM-less scripts using the
    legacy ANSI code page, so non-ASCII characters could be mis-decoded.
#>
param(
    [switch]$WhatIf
)

$ErrorActionPreference = 'Stop'

# script lives in <repo>/tools/
$root = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path -LiteralPath (Join-Path $root 'CMakeLists.txt'))) {
    $root = $PSScriptRoot
}

$exts = @('.c', '.h', '.rc', '.lua', '.mk', '.txt', '.md')
# 排除非源码目录: 构建产物/文档/版本库
$excludeRegex = '[\\/](bin|doc|project|build|cmake-build-[^\\/]+|out|\.git|\.codebuddy)[\\/]'

$utf8Strict = New-Object System.Text.UTF8Encoding($false, $true)
$gbk        = [System.Text.Encoding]::GetEncoding(936)
$utf8NoBom  = New-Object System.Text.UTF8Encoding($false)

$all = @(Get-ChildItem -LiteralPath $root -Recurse -Force | Where-Object { -not $_.PSIsContainer })
Write-Host "root               = $root"
Write-Host "all files found    = $($all.Count)"

$files = @($all | Where-Object {
    ($exts -contains $_.Extension.ToLower()) -and ($_.FullName -notmatch $excludeRegex)
})
Write-Host "selected (by ext)  = $($files.Count)  [$($exts -join ' ')]"

$ascii = 0; $utf8 = 0; $gbkCount = 0; $utf16 = 0; $bom = 0; $converted = 0
$gbkFiles = @()

foreach ($f in $files) {
    $bytes = [System.IO.File]::ReadAllBytes($f.FullName)
    if ($null -eq $bytes -or $bytes.Length -eq 0) { continue }

    $hasBom = ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
    if ($hasBom) {
        $body = New-Object byte[] ($bytes.Length - 3)
        [Array]::Copy($bytes, 3, $body, 0, $body.Length)
        $bom++
    } else {
        $body = $bytes
    }

    # UTF-16 with BOM (FF FE / FE FF): decode accordingly.
    # NOTE: mixed-encoding files (UTF-16 head + ASCII tail, as once found in
    # core/mapper/16.c) are NOT auto-converted here - they must be fixed by hand.
    $isUtf16 = ($body.Length -ge 2) -and
               (($body[0] -eq 0xFF -and $body[1] -eq 0xFE) -or
                ($body[0] -eq 0xFE -and $body[1] -eq 0xFF))

    $isAscii = $false
    if (-not $isUtf16) {
        $isAscii = $true
        foreach ($b in $body) { if ($b -ge 0x80) { $isAscii = $false; break } }
    }

    if ($isUtf16) {
        if ($body[0] -eq 0xFE) {
            $text = [System.Text.Encoding]::BigEndianUnicode.GetString($body)
        } else {
            $text = [System.Text.Encoding]::Unicode.GetString($body)
        }
        $text = $text.TrimStart([char]0xFEFF)
        $utf16++
    } elseif ($isAscii) {
        $text = [System.Text.Encoding]::ASCII.GetString($body)
        $ascii++
    } else {
        try {
            $text = $utf8Strict.GetString($body)
            $utf8++
        } catch {
            $text = $gbk.GetString($body)
            $gbkCount++
            $gbkFiles += $f.FullName
        }
    }

    $textLf = $text -replace "`r`n", "`n"
    $textLf = $textLf -replace "`r", "`n"

    $newBytes = $utf8NoBom.GetBytes($textLf)

    $same = ($body.Length -eq $newBytes.Length)
    if ($same) {
        for ($i = 0; $i -lt $newBytes.Length; $i++) {
            if ($body[$i] -ne $newBytes[$i]) { $same = $false; break }
        }
    }

    if (-not $same) {
        if ($WhatIf) {
            if ($converted -lt 5) { Write-Host "WOULD CONVERT (sample): $($f.FullName)" }
        } else {
            [System.IO.File]::WriteAllBytes($f.FullName, $newBytes)
        }
        $converted++
    }
}

Write-Host '========================================='
Write-Host "pure ASCII         = $ascii"
Write-Host "already UTF-8      = $utf8"
Write-Host "converted from GBK = $gbkCount"
Write-Host "converted from UTF16 = $utf16"
Write-Host "UTF-8 BOM removed  = $bom"
Write-Host "files rewritten    = $converted"
if ($WhatIf) { Write-Host '*** WhatIf mode: no file was modified ***' }
Write-Host '========================================='
if ($gbkFiles.Count -gt 0) {
    Write-Host "--- GBK(936) files (first 20 of $($gbkFiles.Count)) ---"
    $gbkFiles | Select-Object -First 20 | ForEach-Object { Write-Host $_ }
}
