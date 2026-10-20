[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SourceDir,
    [Parameter(Mandatory)][string]$OutputDir,
    [ValidatePattern('^\d+\.\d+\.\d+-preview\.\d+$')][string]$Version = '0.4.0-preview.6',
    [switch]$InstallerTest
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$iscc = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'
if (!(Test-Path -LiteralPath $iscc)) { throw 'Install Inno Setup 6.6 or newer before building the installer.' }
$payload = (Resolve-Path -LiteralPath $SourceDir).Path
$exe = Join-Path $payload 'Elka.MidiPickup.exe'
if ((Get-Item -LiteralPath $exe).VersionInfo.ProductVersion -ne $Version) { throw 'Installer and payload versions differ.' }
$invalid = @(Get-ChildItem -LiteralPath $payload -File -Recurse | Where-Object { $_.Extension -notin @('.exe','.md','.png','.jpg','.ico') })
if ($invalid.Count) { throw 'Installer source contains settings, logs or an unexpected payload type.' }
$binaries = @(Get-ChildItem -LiteralPath $payload -File -Recurse -Filter '*.exe')
if ($binaries.Count -ne 1 -or $binaries[0].FullName -ne $exe) { throw 'Installer source contains an unexpected executable.' }
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$output = (Resolve-Path -LiteralPath $OutputDir).Path
$suffix = if ($InstallerTest) { 'installer-smoketest' } else { 'setup-win-x64-unsigned' }
$installer = Join-Path $output "Elka.MidiPickup-$Version-$suffix.exe"
if (Test-Path -LiteralPath $installer) { throw 'Preserve the existing installer; choose a fresh output folder or version.' }
$numbers = $Version -replace '-preview\.', '.'
$arguments = @('/Qp', "/DReleaseVersion=$Version", "/DVersionNumbers=$numbers", "/DSourceDir=$payload", "/DOutputDir=$output")
if ($InstallerTest) { $arguments += '/DInstallerTest' }
& $iscc @arguments (Join-Path $root 'installer\Elka.MidiPickup.iss')
if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $installer)) { throw 'Inno Setup compilation failed.' }
Write-Output $installer
