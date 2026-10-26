# Generate BMP test samples in every format System.Drawing can produce,
# plus a 24bpp reference for each so we can cross-check our decoder.
#
# For each format we write:
#   samples/<name>.bmp       the sample in that format
#   samples/<name>_ref.bmp   the same image re-encoded to 24bpp by System.Drawing
#
# If our decoder reads <name>.bmp to the same pixels System.Drawing reads out
# of it (the _ref), the decoder is correct.
#
# Note: comments are ASCII-only on purpose. PowerShell 5.1 reads BOM-less
# UTF-8 as ANSI, so non-ASCII here would mojibake.

Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $root "samples"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$W = 320
$H = 240

function New-SourceImage {
    $bmp = New-Object System.Drawing.Bitmap $W, $H, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias

    # horizontal colour gradient, so every channel sweeps its full range
    for ($x = 0; $x -lt $W; $x++) {
        $r = [int](255 * $x / ($W - 1))
        $b = [int](255 * ($W - 1 - $x) / ($W - 1))
        $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, $r, 128, $b))
        $g.DrawLine($pen, $x, 0, $x, $H)
        $pen.Dispose()
    }
    # some solid shapes to give flats and hard edges
    $g.FillEllipse([System.Drawing.Brushes]::Red, 20, 20, 90, 90)
    $g.FillRectangle([System.Drawing.Brushes]::Black, 140, 30, 80, 60)
    $g.FillEllipse([System.Drawing.Brushes]::Yellow, 40, 130, 70, 70)
    $g.FillRectangle([System.Drawing.Brushes]::White, 200, 150, 90, 60)
    # a diagonal for edge detectors
    $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 0, 200, 0)), 6
    $g.DrawLine($pen, 0, $H - 1, $W - 1, 0)
    $pen.Dispose()

    $g.Dispose()
    return $bmp
}

function Save-AsFormat($src, $fmt, $path) {
    # GDI+ refuses to create a Graphics over an indexed bitmap, so we cannot
    # draw straight into 1/4/8bpp. Bitmap.Clone(rect, format) performs the
    # conversion (and the palette quantisation) instead.
    $rect = New-Object System.Drawing.Rectangle 0, 0, $W, $H
    $bmp = $src.Clone($rect, $fmt)
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Bmp)
    $bmp.Dispose()
}

function Save-Reference($path, $refPath) {
    $img = [System.Drawing.Image]::FromFile($path)
    $bmp = New-Object System.Drawing.Bitmap $img.Width, $img.Height, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.DrawImage($img, 0, 0, $img.Width, $img.Height)
    $g.Dispose()
    $bmp.Save($refPath, [System.Drawing.Imaging.ImageFormat]::Bmp)
    $bmp.Dispose()
    $img.Dispose()
}

$src = New-SourceImage

# name -> PixelFormat. 4bpp/1bpp depend on the GDI+ driver being able to
# allocate a DIB of that depth; we try each and report failures rather than dying.
$formats = [ordered]@{
    "f01bpp"    = [System.Drawing.Imaging.PixelFormat]::Format1bppIndexed
    "f04bpp"    = [System.Drawing.Imaging.PixelFormat]::Format4bppIndexed
    "f08bpp"    = [System.Drawing.Imaging.PixelFormat]::Format8bppIndexed
    "f16bpp555" = [System.Drawing.Imaging.PixelFormat]::Format16bppRgb555
    "f16bpp565" = [System.Drawing.Imaging.PixelFormat]::Format16bppRgb565
    "f24bpp"    = [System.Drawing.Imaging.PixelFormat]::Format24bppRgb
    "f32bppRgb" = [System.Drawing.Imaging.PixelFormat]::Format32bppRgb
    "f32bppArgb" = [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
}

foreach ($name in $formats.Keys) {
    $path = Join-Path $outDir "$name.bmp"
    $refPath = Join-Path $outDir "${name}_ref.bmp"
    try {
        Save-AsFormat $src $formats[$name] $path
        Save-Reference $path $refPath
        $len = (Get-Item $path).Length
        Write-Output ("OK    {0,-11} {1,8} bytes" -f $name, $len)
    } catch {
        Write-Output ("SKIP  {0,-11} {1}" -f $name, $_.Exception.Message)
    }
}

$src.Dispose()
Write-Output "samples written to $outDir"
