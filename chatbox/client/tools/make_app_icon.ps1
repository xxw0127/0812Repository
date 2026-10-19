# Generates resources/app.ico (ChatBox brand mark, multi-frame) from vector
# geometry. No third-party dependencies: PowerShell + System.Drawing only.
#
# Geometry spec: docs/superpowers/specs/2026-10-19-client-brand-icons.md
#   full mark  (32/48/64/128/256): blue tile + white bubble + punched C
#   simplified (16/24)           : blue tile + white bubble, no C
#
# The old app.ico had a non-standard ICONDIR (planes=0, bitCount=0,
# reserved=0xdf) which Qt refuses to read; this writer emits a standard one.
#
# Usage: powershell -File tools/make_app_icon.ps1 [-OutputPath <path>]
[CmdletBinding()]
param(
    [string]$OutputPath = ''
)

$ErrorActionPreference = 'Stop'
if (-not $OutputPath) {
    # $PSScriptRoot is unreliable while the param block evaluates under -File
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    $OutputPath = Join-Path (Split-Path -Parent $scriptDir) 'resources\app.ico'
}
Add-Type -AssemblyName System.Drawing

$Blue = [System.Drawing.Color]::FromArgb(255, 0, 82, 255)   # theme::kAccent #0052FF
$White = [System.Drawing.Color]::White
$Supersample = 4                                             # draw 4x, then shrink

function New-RoundedRectPath {
    param([double]$X, [double]$Y, [double]$W, [double]$H, [double]$R)
    $d = 2 * $R
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc($X, $Y, $d, $d, 180, 90)
    $path.AddArc($X + $W - $d, $Y, $d, $d, 270, 90)
    $path.AddArc($X + $W - $d, $Y + $H - $d, $d, $d, 0, 90)
    $path.AddArc($X, $Y + $H - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    return $path
}

function New-MarkBitmap {
    param([int]$Px, [bool]$Simplified)

    $ss = $Px * $Supersample
    $big = New-Object System.Drawing.Bitmap $ss, $ss, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($big)
    try {
        $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $g.ScaleTransform($ss / 256.0, $ss / 256.0)

        $blueBrush = New-Object System.Drawing.SolidBrush $Blue
        $whiteBrush = New-Object System.Drawing.SolidBrush $White

        # tile
        $g.FillPath($blueBrush, (New-RoundedRectPath 0 0 256 256 56))

        # bubble body (+ tail width per variant)
        if ($Simplified) {
            # 16/24px: smaller bubble, chunkier tail so the silhouette still
            # reads as a speech bubble when shrunk into the tray
            $g.FillPath($whiteBrush, (New-RoundedRectPath 36 42 184 156 56))
            $tailWidth = 20
        } else {
            $g.FillPath($whiteBrush, (New-RoundedRectPath 30 36 196 170 62))
            $tailWidth = 16
        }

        # bubble tail: filled triangle, stroked with round joins for the soft tip
        $tail = New-Object System.Drawing.Drawing2D.GraphicsPath
        if ($Simplified) {
            $tail.AddPolygon(@(
                (New-Object System.Drawing.PointF 88, 182),
                (New-Object System.Drawing.PointF 74, 226),
                (New-Object System.Drawing.PointF 130, 190)))
        } else {
            $tail.AddPolygon(@(
                (New-Object System.Drawing.PointF 76, 196),
                (New-Object System.Drawing.PointF 60, 240),
                (New-Object System.Drawing.PointF 124, 200)))
        }
        $tailPen = New-Object System.Drawing.Pen $White, ([single]$tailWidth)
        $tailPen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
        $g.FillPath($whiteBrush, $tail)
        $g.DrawPath($tailPen, $tail)

        # punched C: centre (128,121) r=42, 270 degrees counter-clockwise from -45
        if (-not $Simplified) {
            $cPen = New-Object System.Drawing.Pen $Blue, ([single]26)
            $cPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
            $cPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
            $g.DrawArc($cPen, 86, 79, 84, 84, -45, -270)
            $cPen.Dispose()
        }
        $tailPen.Dispose()
        $blueBrush.Dispose()
        $whiteBrush.Dispose()
    } finally {
        $g.Dispose()
    }

    $small = New-Object System.Drawing.Bitmap $Px, $Px, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g2 = [System.Drawing.Graphics]::FromImage($small)
    try {
        $g2.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g2.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $g2.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
        $rect = New-Object System.Drawing.Rectangle 0, 0, $Px, $Px
        $g2.DrawImage($big, $rect, 0, 0, $ss, $ss, [System.Drawing.GraphicsUnit]::Pixel)
    } finally {
        $g2.Dispose()
        $big.Dispose()
    }
    return $small
}

function Get-DibBytes {
    param([System.Drawing.Bitmap]$Bitmap)
    $px = $Bitmap.Width
    $rect = New-Object System.Drawing.Rectangle 0, 0, $px, $px
    $locked = $Bitmap.LockBits($rect,
        [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $stride = $locked.Stride
    $raw = New-Object byte[] ($stride * $px)
    try {
        [System.Runtime.InteropServices.Marshal]::Copy($locked.Scan0, $raw, 0, $raw.Length)
    } finally {
        $Bitmap.UnlockBits($locked)
    }

    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter $ms
    # BITMAPINFOHEADER (height doubled: XOR bitmap + AND mask)
    $bw.Write([uint32]40)
    $bw.Write([int32]$px)
    $bw.Write([int32](2 * $px))
    $bw.Write([uint16]1)                  # planes
    $bw.Write([uint16]32)                 # bit count
    $bw.Write([uint32]0)                  # BI_RGB
    $bw.Write([uint32]($px * $px * 4))
    $bw.Write([int32]0); $bw.Write([int32]0)
    $bw.Write([uint32]0); $bw.Write([uint32]0)
    # XOR pixels, bottom-up BGRA rows
    for ($y = $px - 1; $y -ge 0; $y--) {
        $bw.Write($raw, $y * $stride, $px * 4)
    }
    # AND mask: zeroed (alpha channel carries the shape), rows padded to 4 bytes
    $maskRow = [int]([Math]::Floor(($px + 31) / 32) * 4)
    $empty = New-Object byte[] $maskRow
    for ($y = 0; $y -lt $px; $y++) {
        $bw.Write($empty, 0, $maskRow)
    }
    $bw.Flush()
    return $ms.ToArray()
}

function Get-PngBytes {
    param([System.Drawing.Bitmap]$Bitmap)
    $ms = New-Object System.IO.MemoryStream
    $Bitmap.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    return $ms.ToArray()
}

$frames = @(
    @{ Size = 16;  Simplified = $true;  Png = $false },
    @{ Size = 24;  Simplified = $true;  Png = $false },
    @{ Size = 32;  Simplified = $false; Png = $false },
    @{ Size = 48;  Simplified = $false; Png = $false },
    @{ Size = 64;  Simplified = $false; Png = $false },
    @{ Size = 128; Simplified = $false; Png = $false },
    @{ Size = 256; Simplified = $false; Png = $true }
)

$images = @()
foreach ($frame in $frames) {
    $bitmap = New-MarkBitmap -Px $frame.Size -Simplified $frame.Simplified
    try {
        $bytes = if ($frame.Png) { Get-PngBytes -Bitmap $bitmap } else { Get-DibBytes -Bitmap $bitmap }
    } finally {
        $bitmap.Dispose()
    }
    $images += [pscustomobject]@{ Size = $frame.Size; Bytes = $bytes; IsPng = [bool]$frame.Png }
}

$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $ms
$bw.Write([uint16]0)                      # reserved
$bw.Write([uint16]1)                      # type: icon
$bw.Write([uint16]$images.Count)
$offset = 6 + 16 * $images.Count
foreach ($image in $images) {
    $dim = if ($image.Size -ge 256) { 0 } else { $image.Size }
    $bw.Write([byte]$dim); $bw.Write([byte]$dim)
    $bw.Write([byte]0); $bw.Write([byte]0)          # colour count, reserved
    $bw.Write([uint16]1)                            # planes
    $bw.Write([uint16]32)                           # bit count
    $bw.Write([uint32]$image.Bytes.Length)
    $bw.Write([uint32]$offset)
    $offset += $image.Bytes.Length
}
foreach ($image in $images) {
    $bw.Write($image.Bytes, 0, $image.Bytes.Length)
}
$bw.Flush()
[System.IO.File]::WriteAllBytes($OutputPath, $ms.ToArray())

# self-check: reparse the written ICONDIR independently of any image library
$check = [System.IO.File]::ReadAllBytes($OutputPath)
$count = [System.BitConverter]::ToUInt16($check, 4)
$parts = @()
for ($i = 0; $i -lt $count; $i++) {
    $o = 6 + $i * 16
    $w = if ($check[$o] -eq 0) { 256 } else { $check[$o] }
    $h = if ($check[$o + 1] -eq 0) { 256 } else { $check[$o + 1] }
    $off = [System.BitConverter]::ToUInt32($check, $o + 12)
    $format = if ($off + 4 -le $check.Length -and [System.BitConverter]::ToUInt32($check, $off) -eq 0x474E5089) { 'png' } else { 'dib' }
    $parts += "${w}x${h}($format)"
}
Write-Host "frames=$count  $($parts -join ',')"
Write-Host "written: $OutputPath ($($check.Length) bytes)"
if ($count -ne $images.Count) {
    throw "frame count mismatch after write"
}
