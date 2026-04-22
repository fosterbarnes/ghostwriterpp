# Inno Setup (ISCC.exe) on PATH. `.buildRelease.ps1` runs this after zipping; or run standalone after `build-release\bin` is ready.
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "scriptHelper.ps1")
$root = Split-Path $PSScriptRoot -Parent
Push-Location -LiteralPath $root
try {
    $ver = Read-VersionFileFromScriptsRoot -ScriptsDirectory $PSScriptRoot

    $out = Join-Path $root ".installer\Output"
    if (Test-Path -LiteralPath $out) {
        Write-Host "Cleaning $out"
        Remove-Item -LiteralPath $out -Recurse -Force
    }
    New-Item -ItemType Directory -Path $out -Force | Out-Null

    $iss = Join-Path $root ".installer\ghostwriterpp.x64.installer.iss"
    Write-Host "Building x64 installer (AppVersion=$ver)"
    $iscc = Get-Command ISCC.exe -ErrorAction Stop
    Invoke-NativeCommand -What "ISCC" -FilePath $iscc.Source -ArgumentList @("/DAppVersion=$ver", $iss)
    Write-Host "Done. Output: $out"
}
finally {
    Pop-Location
}
