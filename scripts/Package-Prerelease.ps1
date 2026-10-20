[CmdletBinding()]
param([ValidatePattern('^\d+\.\d+\.\d+-preview\.\d+$')][string]$Version = '0.4.0-preview.5')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$output = Join-Path $root "artifacts\releases\$Version"
if (Test-Path -LiteralPath $output) { throw 'This release folder already exists. Preserve it and use a new version or a new checkout.' }

& (Join-Path $PSScriptRoot 'Build.ps1') -Configuration Release
& (Join-Path $root 'artifacts\x64\Release\Elka.MidiPickup.Tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Regression checks failed.' }
$exe = Join-Path $root 'artifacts\x64\Release\Elka.MidiPickup.exe'
if ((Get-Item -LiteralPath $exe).VersionInfo.ProductVersion -ne $Version) { throw 'EXE version does not match the requested release.' }
if ((Get-AuthenticodeSignature -FilePath $exe).Status -ne 'NotSigned') { throw 'Review the unsigned package naming before packaging a signed executable.' }

$portable = Join-Path $output 'portable'
$artwork = Join-Path $output 'artwork'
New-Item -ItemType Directory -Path $portable,$artwork -Force | Out-Null
Copy-Item -LiteralPath $exe -Destination $portable
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $portable
Copy-Item -LiteralPath (Join-Path $root 'docs') -Destination (Join-Path $portable 'docs') -Recurse
New-Item -ItemType Directory -Path (Join-Path $portable 'app') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'app\Assets') -Destination (Join-Path $portable 'app\Assets') -Recurse
New-Item -ItemType Directory -Path (Join-Path $artwork 'docs'),(Join-Path $artwork 'app') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'docs\images') -Destination (Join-Path $artwork 'docs\images') -Recurse
Copy-Item -LiteralPath (Join-Path $root 'docs\Artwork.md') -Destination (Join-Path $artwork 'docs\Artwork.md')
Copy-Item -LiteralPath (Join-Path $root 'app\Assets') -Destination (Join-Path $artwork 'app\Assets') -Recurse

$allowed = @('.exe','.md','.png','.jpg','.ico')
$unexpected = Get-ChildItem -LiteralPath $portable,$artwork -Recurse -File | Where-Object Extension -notin $allowed
if ($unexpected) { throw 'An unexpected file type entered release staging.' }
$executables = @(Get-ChildItem -LiteralPath $portable -Recurse -File -Filter '*.exe')
if ($executables.Count -ne 1 -or $executables[0].Name -ne 'Elka.MidiPickup.exe') { throw 'Unexpected executable in portable package.' }

$zip = Join-Path $output "Elka.MidiPickup-$Version-win-x64-unsigned.zip"
$artZip = Join-Path $output "Elka.MidiPickup-$Version-artwork.zip"
Compress-Archive -Path (Join-Path $portable '*') -DestinationPath $zip
Compress-Archive -Path (Join-Path $artwork '*') -DestinationPath $artZip
$sums = foreach ($path in @($zip,$artZip,(Join-Path $portable 'Elka.MidiPickup.exe'))) {
    "{0}  {1}" -f (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant(), (Split-Path $path -Leaf)
}
Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Value $sums -Encoding ascii
Write-Output "Portable prerelease staged at $output"
