[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$projectDir = Split-Path $PSScriptRoot -Parent
$cacheDir = Join-Path $projectDir '.cache'
$packageName = 'Windows.Devices.Midi2.0.99.92-preview.11.nupkg'
$expectedHash = '13afc6e6da2adac309ab8c174352f46f1c0ebe2d79e65475bd6274defb73df74'
$packagePath = Join-Path $cacheDir $packageName
$sdkDir = Join-Path $cacheDir 'midi-sdk'
$generatedDir = Join-Path $cacheDir 'generated'
New-Item -ItemType Directory -Path $cacheDir -Force | Out-Null
if (!(Test-Path -LiteralPath $packagePath)) {
    Write-Host 'Downloading the official Microsoft MIDI Preview 11 development package (project-local only)...'
    Invoke-WebRequest -UseBasicParsing -Uri "https://github.com/microsoft/MIDI/releases/download/inbox-preview-11/$packageName" -OutFile $packagePath
}
if ((Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedHash) {
    throw 'MIDI development package SHA-256 mismatch. No package code has been executed.'
}
if (!(Test-Path -LiteralPath "$sdkDir\ref\native\Windows.Devices.Midi2.winmd")) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($packagePath, $sdkDir)
}
if (!(Test-Path -LiteralPath "$generatedDir\winrt\Windows.Devices.Midi2.h")) {
    $kitsDir = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
    $generator = Join-Path $kitsDir 'bin\10.0.26100.0\x64\cppwinrt.exe'
    $metadata = Join-Path $kitsDir 'UnionMetadata\10.0.26100.0\Windows.winmd'
    if (!(Test-Path -LiteralPath $generator) -or !(Test-Path -LiteralPath $metadata)) {
        throw 'Install Windows SDK 10.0.26100.0 using Visual Studio Installer, then build again.'
    }
    & $generator -input "$sdkDir\ref\native\Windows.Devices.Midi2.winmd" -reference $metadata -output $generatedDir
    if ($LASTEXITCODE -ne 0) { throw 'C++/WinRT header generation failed.' }
}
