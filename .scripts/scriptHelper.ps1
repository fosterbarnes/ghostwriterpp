# SPDX-License-Identifier: GPL-3.0-or-later
# Shared helpers for build.ps1, .run.ps1, .buildRelease.ps1, buildInstaller.ps1

$script:GwPresetBuildDirs = @{
    dev                        = "build"
    release                    = "build-release"
    asan                       = "build-asan"
    unity                      = "build-unity"
    profile                    = "build-profile"
    clazy                      = "build-clazy"
    "dev-disable-deprecated"   = "build-disable-deprecated"
}

function Get-BuildDirectoryNameForPreset {
    param([Parameter(Mandatory)][string]$Preset)
    $dir = $script:GwPresetBuildDirs[$Preset]
    if (-not $dir) { throw "Unknown CMake preset (no build dir mapping): $Preset" }
    return $dir
}

function Invoke-NativeCommand {
    param(
        [Parameter(Mandatory)][string]$What,
        [Parameter(Mandatory)][string]$FilePath,
        [Parameter(Mandatory)][string[]]$ArgumentList,
        [string]$FailureMessage
    )
    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        if ($FailureMessage) { throw $FailureMessage }
        throw "$What failed (exit $LASTEXITCODE): $FilePath $($ArgumentList -join ' ')"
    }
}

function Test-HasQtOnPath {
    if (-not $env:PATH) { return $false }
    foreach ($p in $env:PATH.Split([IO.Path]::PathSeparator)) {
        if ($p -and (Test-Path (Join-Path $p 'Qt6Core.dll'))) { return $true }
    }
    $false
}

function Ensure-QtRuntimeFromCraft {
    param(
        [Parameter(Mandatory)][string]$ProjectRoot,
        [Parameter(Mandatory)][string]$CraftRoot,
        [switch]$Strict
    )
    if (Test-HasQtOnPath) { return }
    $craftEnv = Join-Path $CraftRoot 'craft\craftenv.ps1'
    if (Test-Path $craftEnv) {
        Write-Host "Sourcing Craft environment: $craftEnv"
        . $craftEnv
        Set-Location $ProjectRoot
        if (-not (Test-HasQtOnPath)) {
            $msg = "Qt6Core.dll not on PATH. Source Craft (craftenv.ps1), set QTDIR, or add Qt 6 bin to PATH."
            if ($Strict) { throw $msg }
            Write-Warning "Sourced craftenv.ps1 but Qt6Core.dll still not on PATH. The app may fail with missing DLL errors."
        }
        return
    }
    if ($Strict) {
        throw "Qt6Core.dll not on PATH. Source Craft (craftenv.ps1), set QTDIR, or add Qt 6 bin to PATH."
    }
    Write-Warning "Qt6Core.dll not on PATH and craftenv.ps1 not found at $craftEnv. The app may fail with missing DLL errors."
}

function Read-VersionFileFromScriptsRoot {
    param([Parameter(Mandatory)][string]$ScriptsDirectory)
    $versionPath = Join-Path $ScriptsDirectory "version"
    if (-not (Test-Path -LiteralPath $versionPath)) { throw "Version file not found: $versionPath" }
    $ver = ([IO.File]::ReadAllText($versionPath)).Trim()
    if ([string]::IsNullOrWhiteSpace($ver)) { throw "Version file is empty: $versionPath" }
    return $ver
}
