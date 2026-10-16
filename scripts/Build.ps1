[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [switch]$IncludeProbe
)
$ErrorActionPreference = 'Stop'
$projectDir = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsDir = & $vswhere -latest -products '*' -version '[18.0,19.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsDir) { throw 'Visual Studio 2026 C++ build tools (v145) are required.' }
$projects = @('app\Elka.MidiPickup.App.vcxproj', 'tests\Elka.MidiPickup.Tests.vcxproj')
if ($IncludeProbe) { $projects += 'src\Elka.MidiPickup.Probe.vcxproj' }
foreach ($project in $projects) {
    & "$vsDir\MSBuild\Current\Bin\MSBuild.exe" (Join-Path $projectDir $project) /m /nologo /v:minimal "/p:Configuration=$Configuration" /p:Platform=x64
    if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE." }
}
