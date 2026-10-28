[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path $PSScriptRoot -Parent
$imageDir = Join-Path $root 'docs\images'
$encoder = [Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object MimeType -eq 'image/jpeg'
$parameters = [Drawing.Imaging.EncoderParameters]::new(1)
$parameters.Param[0] = [Drawing.Imaging.EncoderParameter]::new([Drawing.Imaging.Encoder]::Quality, [long]92)
$rows = @()
try {
    foreach ($orientation in @('horizontal', 'vertical')) {
        $name = "midi-pickup-$orientation"
        $png = Join-Path $imageDir "$name.png"
        $jpg = Join-Path $imageDir "$name-under-1mb.jpg"
        $bitmap = [Drawing.Image]::FromFile($png)
        try {
            $expected = if ($orientation -eq 'horizontal') { @(1774, 887) } else { @(941, 1672) }
            if ($bitmap.Width -ne $expected[0] -or $bitmap.Height -ne $expected[1]) { throw "Unexpected master dimensions: $name" }
            $bitmap.Save($jpg, $encoder, $parameters)
            if ((Get-Item -LiteralPath $jpg).Length -ge 1000000) { throw "$jpg exceeds the 1 MB target." }
            foreach ($asset in @($png, $jpg)) {
                $file = Get-Item -LiteralPath $asset
                $rows += "| [$($file.Name)](images/$($file.Name)) | $($bitmap.Width) x $($bitmap.Height) | $($file.Length.ToString('N0', [Globalization.CultureInfo]::InvariantCulture)) |"
            }
        } finally { $bitmap.Dispose() }
    }
} finally { $parameters.Dispose() }
$document = @'
# Header artwork

Matching ElkaSoft headers for the MIDI Pickup prerelease, using the approved app icon and a warm charcoal, amber and muted rose palette.

| File | Pixels | Bytes |
| --- | --- | ---: |
'@
$document += "`n" + ($rows -join "`n") + "`n"
$document += @'

The horizontal format is 2:1; the vertical format is approximately 9:16, matching the established audio-app artwork sizes. Both JPEG files are below **1,000,000 bytes** (decimal 1 MB). PNG files are full-quality masters and may exceed that size.

JPEG exports use quality 92 without resizing or cropping. Recreate them with `scripts/Export-Artwork.ps1`. The source PNGs are preserved.

The README uses the horizontal JPEG. The artwork release ZIP includes both orientations, both file formats and the app icon.
'@
Set-Content -LiteralPath (Join-Path $root 'docs\Artwork.md') -Value $document -Encoding utf8
Write-Output ($rows -join "`n")
