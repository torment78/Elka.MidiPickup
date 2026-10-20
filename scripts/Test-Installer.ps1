[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SourceDir,
    [ValidatePattern('^\d+\.\d+\.\d+-preview\.\d+$')][string]$Version = '0.4.0-preview.6'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$fixture = Join-Path $root ('artifacts\installer-tests\' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $fixture | Out-Null
$target = Join-Path $fixture 'Installed'
& (Join-Path $PSScriptRoot 'Build-Installer.ps1') -SourceDir $SourceDir -OutputDir $fixture -Version $Version -InstallerTest
$setup = Join-Path $fixture "Elka.MidiPickup-$Version-installer-smoketest.exe"
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$runBefore = (Get-ItemProperty -LiteralPath $runKey -Name ElkaMidiPickup -ErrorAction SilentlyContinue).ElkaMidiPickup
$registry = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\ElkaMidiPickupInstallerSmokeTest_is1'
if (Test-Path -LiteralPath $registry) { throw 'A previous installer fixture is still registered; review it before retrying.' }
function Install-Fixture([string]$phase) {
    $arguments = @('/CURRENTUSER', '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', '/NOICONS', '/TASKS=""',
        ('/DIR="' + $target + '"'), ('/LOG="' + (Join-Path $fixture "$phase.log") + '"'))
    $process = Start-Process -FilePath $setup -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (!$process.WaitForExit(60000)) { throw "Installer timed out during $phase. Inspect the fixture before retrying." }
    if ($process.ExitCode -ne 0) { throw "Installer failed during $phase with code $($process.ExitCode)." }
}
Install-Fixture 'install'
$installedExe = Join-Path $target 'Elka.MidiPickup.exe'
if ((Get-FileHash -LiteralPath $installedExe).Hash -ne (Get-FileHash -LiteralPath (Join-Path $SourceDir 'Elka.MidiPickup.exe')).Hash) { throw 'Installed EXE differs from the package.' }
if (!(Test-Path -LiteralPath (Join-Path $target 'Elka.MidiPickup.installed'))) { throw 'Installed storage marker is missing.' }
if (!(Test-Path -LiteralPath $registry)) { throw 'Windows uninstall registration is missing.' }
if ((Get-ItemProperty -LiteralPath $registry).InstallLocation.TrimEnd('\') -ne $target) { throw 'Unexpected uninstall install location.' }
if (Get-ChildItem -LiteralPath $target -Recurse -File | Where-Object { $_.Extension -in @('.ini','.applications','.log','.pdb','.obj') }) { throw 'Personal data or build output entered the installer.' }
$sentinel = Join-Path $target 'user-created-settings.keep'
[IO.File]::WriteAllText($sentinel, 'Preserve this user-owned file across install, upgrade and uninstall.')
$sentinelHash = (Get-FileHash -LiteralPath $sentinel).Hash
Install-Fixture 'reinstall'
if ((Get-FileHash -LiteralPath $sentinel).Hash -ne $sentinelHash) { throw 'Reinstall changed a user-created file.' }
if ((Get-FileHash -LiteralPath $installedExe).Hash -ne (Get-FileHash -LiteralPath (Join-Path $SourceDir 'Elka.MidiPickup.exe')).Hash) { throw 'Reinstalled EXE differs.' }
$uninstall = Join-Path $target 'unins000.exe'
$arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', ('/LOG="' + (Join-Path $fixture 'uninstall.log') + '"'))
$process = Start-Process -FilePath $uninstall -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (!$process.WaitForExit(60000) -or $process.ExitCode -ne 0) { throw 'Fixture uninstall failed; inspect logs.' }
if (Test-Path -LiteralPath $installedExe) { throw 'Uninstall left its program executable.' }
if (Test-Path -LiteralPath (Join-Path $target 'Elka.MidiPickup.installed')) { throw 'Uninstall left its storage marker.' }
if (Test-Path -LiteralPath $registry) { throw 'Uninstall registration was not removed.' }
if ((Get-FileHash -LiteralPath $sentinel).Hash -ne $sentinelHash) { throw 'Uninstall removed user data.' }
$runAfter = (Get-ItemProperty -LiteralPath $runKey -Name ElkaMidiPickup -ErrorAction SilentlyContinue).ElkaMidiPickup
if ($runBefore -cne $runAfter) { throw 'Installer modified another copy''s Windows-startup entry.' }
$report = "PASS: install/reinstall/uninstall; exact payload; no private settings; retained user file; other startup entry unchanged. Audio app was never launched."
$report | Set-Content -LiteralPath (Join-Path $fixture 'result.txt') -Encoding utf8
Write-Output $report
Write-Output "Evidence: $fixture"
