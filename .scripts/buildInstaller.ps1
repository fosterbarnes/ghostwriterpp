#requires -Version 7.0
param([Alias('h')][switch]$Help, [string]$Architecture)
$ErrorActionPreference = 'Stop'
if ($Help) { Write-Host 'buildInstaller.ps1 [-x86|-x64|-arm64]'; return }
. "$PSScriptRoot\scriptHelper.ps1"
Write-Host "--- building $projectName installer... ---"
Set-Location -LiteralPath $repoRoot
$null = getArchitecture @($Architecture)
$exePath = Join-Path $repoRoot 'build-release\bin\ghostwriter++.exe'
if (-not (Test-Path -LiteralPath $exePath)) { throw "Missing publish output: $exePath" }
$iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue)?.Source
if (-not $iscc) { $iscc = 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe' }
if (-not (Test-Path -LiteralPath $iscc)) { throw "Inno Setup compiler not found: $iscc" }
deleteDir $installerOutput
New-Item -ItemType Directory -Path $installerOutput -Force | Out-Null
$iss = "$repoRoot\.installer\ghostwriterpp.x64.installer.iss"
runNativeCommand $iscc @("/DAppVersion=$versionContents", $iss) 'ISCC x64'
closeOut 3
