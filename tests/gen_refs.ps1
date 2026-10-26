# gen_refs.ps1 -- re-encode every BMP in a directory to 24bpp via System.Drawing.
#
# The 24bpp copy serves as an INDEPENDENT reference: System.Drawing's own BMP
# reader interprets formats we generate by hand (RLE, CORE header, V4/V5,
# top-down), so comparing pixel checksums against it validates our decoder
# without us grading our own homework.
#
# ASCII-only comments on purpose: PowerShell 5.1 reads BOM-less UTF-8 as ANSI.

param(
    [string]$Dir = "samples/exotic"
)

Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
$full = Join-Path $root $Dir

if (-not (Test-Path $full)) {
    Write-Output "directory not found: $full"
    exit 1
}

$count = 0
foreach ($f in Get-ChildItem -Path $full -Filter *.bmp) {
    if ($f.BaseName.EndsWith("_ref")) { continue }

    $refPath = Join-Path $full ($f.BaseName + "_ref.bmp")
    try {
        $img = [System.Drawing.Image]::FromFile($f.FullName)
        $bmp = New-Object System.Drawing.Bitmap $img.Width, $img.Height, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.DrawImage($img, 0, 0, $img.Width, $img.Height)
        $g.Dispose()
        $bmp.Save($refPath, [System.Drawing.Imaging.ImageFormat]::Bmp)
        $bmp.Dispose()
        $img.Dispose()
        $count++
    } catch {
        # malformed samples (bad bpp, truncated) are expected to fail here
        Write-Output ("skip  {0}" -f $f.Name)
    }
}

Write-Output "wrote $count reference(s) into $full"
