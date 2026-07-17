# Inno Setup (ISCC.exe) on PATH. `.prePush.ps1` runs this after buildUpdater; or run standalone after `build-release\bin` is ready.
$ErrorActionPreference = "Stop"
. "$PSScriptRoot\scriptHelper.ps1"
$repoRoot = Get-CMakeProjectRoot -ScriptsDirectory $PSScriptRoot
Push-Location -LiteralPath $repoRoot
try {
    $ver = Read-VersionFile

    $out = Join-Path $repoRoot ".installer\Output"
    if (Test-Path -LiteralPath $out) {
        Write-Host "Cleaning $out"
        Remove-Item -LiteralPath $out -Recurse -Force
    }
    New-Item -ItemType Directory -Path $out -Force | Out-Null

    $iss = Join-Path $repoRoot ".installer\ghostwriterpp.x64.installer.iss"
    Write-Host "Building x64 installer (AppVersion=$ver)"
    $iscc = Get-Command ISCC.exe -ErrorAction Stop
    Invoke-NativeCommand -What "ISCC" -FilePath $iscc.Source -ArgumentList @("/DAppVersion=$ver", $iss)
    Write-Host "Done. Output: $out"
}
finally {
    Pop-Location
}
