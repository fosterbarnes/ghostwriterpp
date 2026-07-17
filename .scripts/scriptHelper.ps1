# SPDX-License-Identifier: GPL-3.0-or-later
#. "$PSScriptRoot\scriptHelper.ps1"
$root = Split-Path -Path $PSScriptRoot -Parent
$version = "$root\VERSION.txt"
$buildNotes = "$root\buildNotes.txt"
$scripts = "$root\.scripts"
$readme = "$root\README.md"

$script:GwPresetBuildDirs = @{
    dev                        = "build"
    release                    = "build-release"
    asan                       = "build-asan"
    unity                      = "build-unity"
    profile                    = "build-profile"
    clazy                      = "build-clazy"
    "dev-disable-deprecated"   = "build-disable-deprecated"
}

$Utf8NoBomEncoding = New-Object System.Text.UTF8Encoding $false

function Write-RepoUtf8NoBomFile {
    param(
        [Parameter(Mandatory)][string]$LiteralPath,
        [Parameter(Mandatory)][string]$Content
    )
    [System.IO.File]::WriteAllText($LiteralPath, $Content, $Utf8NoBomEncoding)
}

function Read-VersionFile {
    param([string]$LiteralPath = $version)
    if (-not (Test-Path -LiteralPath $LiteralPath)) { throw "Version file not found: $LiteralPath" }
    $ver = ([IO.File]::ReadAllText($LiteralPath)).Trim()
    if ([string]::IsNullOrWhiteSpace($ver)) { throw "Version file is empty: $LiteralPath" }
    return $ver
}

function Write-VersionFile {
    param(
        [Parameter(Mandatory)][string]$SemVer,
        [string]$LiteralPath = $version
    )
    $content = $SemVer.Trim() + [Environment]::NewLine
    Write-RepoUtf8NoBomFile -LiteralPath $LiteralPath -Content $content
    $script:versionContents = $SemVer.Trim()
}

$versionContents = Read-VersionFile
$buildNotesContents = if (Test-Path -LiteralPath $buildNotes) {
    [System.IO.File]::ReadAllText($buildNotes).Trim()
} else { "" }

function Get-BuildDirectoryNameForPreset {
    param([Parameter(Mandatory)][string]$Preset)
    $dir = $script:GwPresetBuildDirs[$Preset]
    if (-not $dir) { throw "Unknown CMake preset (no build dir mapping): $Preset" }
    return $dir
}

function Get-CMakeProjectRoot {
    param(
        [Parameter(Mandatory)] [string] $ScriptsDirectory,
        [string] $RepoRoot = "",
        [switch] $UseWorkingDirectory
    )
    $scriptRepoRoot = (Resolve-Path (Join-Path $ScriptsDirectory '..')).Path

    if ($RepoRoot) {
        return (Resolve-Path -LiteralPath $RepoRoot).Path
    }
    if ($UseWorkingDirectory) {
        $cwd = (Get-Location).Path
        if (-not (Test-Path -LiteralPath (Join-Path $cwd 'CMakeLists.txt'))) {
            throw "-UseWorkingDirectory: not a CMake project root (missing CMakeLists.txt): $cwd"
        }
        return $cwd
    }

    $cwd = (Get-Location).Path
    if (($cwd -ne $scriptRepoRoot) -and
        (Test-Path -LiteralPath (Join-Path $cwd 'CMakeLists.txt')) -and
        (Test-Path -LiteralPath (Join-Path $cwd 'src\CMakeLists.txt'))) {
        Write-Warning @"
Using current directory as repository root: $cwd
(The script file lives under: $scriptRepoRoot - that tree will not be built unless you cd there or pass -RepoRoot.)
"@
        return $cwd
    }

    return $scriptRepoRoot
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
