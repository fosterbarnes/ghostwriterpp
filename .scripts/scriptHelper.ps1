# SPDX-License-Identifier: GPL-3.0-or-later
#requires -Version 7.0
$ErrorActionPreference = 'Stop'

#. "$PSScriptRoot\scriptHelper.ps1"
$repoRoot = Split-Path -Path $PSScriptRoot -Parent
$projectName = 'ghostwriterpp'
$appPublisher = 'fosterbarnes'
$ghRepo = "$appPublisher/$projectName"
$appURL = "https://github.com/$ghRepo"
$versionFolder = "$repoRoot\.version"
$version = "$versionFolder\version"
$versionBuild = "$versionFolder\versionBuild"
$versionTag = "$versionFolder\versionTag"
$buildNotes = "$repoRoot\buildNotes.txt"
$scripts = "$repoRoot\.scripts"
$readme = "$repoRoot\README.md"
$publishFolder = "$repoRoot\publish"
$installerOutput = "$repoRoot\.installer\Output"
$weztermExe = (Get-Command wezterm.exe -ErrorAction SilentlyContinue)?.Source

$script:GwPresetBuildDirs = @{
    dev                      = 'build'
    release                  = 'build-release'
    asan                     = 'build-asan'
    unity                    = 'build-unity'
    profile                  = 'build-profile'
    clazy                    = 'build-clazy'
    'dev-disable-deprecated' = 'build-disable-deprecated'
}

$noBom = New-Object System.Text.UTF8Encoding $false

function writeFileNoBom {
    param(
        [Parameter(Mandatory)][string]$LiteralPath,
        [Parameter(Mandatory)][string]$Content
    )
    [System.IO.File]::WriteAllText($LiteralPath, $Content, $noBom)
}

function Write-RepoUtf8NoBomFile {
    param(
        [Parameter(Mandatory)][string]$LiteralPath,
        [Parameter(Mandatory)][string]$Content
    )
    writeFileNoBom -LiteralPath $LiteralPath -Content $Content
}

function readVerFile {
    param([string]$LiteralPath = $version)
    if (-not (Test-Path -LiteralPath $LiteralPath)) { throw "Version file not found: $LiteralPath" }
    $lines = @(([IO.File]::ReadAllText($LiteralPath) -split '\r?\n' | ForEach-Object { $_.Trim() }))
    while ($lines.Count -lt 3) { $lines += '' }
    if ([string]::IsNullOrWhiteSpace($lines[0])) { throw "Version line 1 is empty: $LiteralPath" }
    $lines
}

function writeVerFile {
    param(
        [Parameter(Mandatory)][string]$SemVer,
        [Parameter(Mandatory)][AllowEmptyString()][string]$Tag,
        [Parameter(Mandatory)][AllowEmptyString()][string]$Build
    )
    writeFileNoBom -LiteralPath $version -Content (($SemVer.Trim(), $Tag.Trim(), $Build.Trim()) -join "`n")
    $script:versionContents = $SemVer.Trim()
    $script:tag = if ($Tag.Trim()) { $Tag.Trim() } elseif (Test-Path -LiteralPath $versionTag) {
        ([IO.File]::ReadAllText($versionTag)).Trim()
    } else { "v$($SemVer.Trim())" }
}

function Read-VersionFile {
    param([string]$LiteralPath = $version)
    (readVerFile -LiteralPath $LiteralPath)[0]
}

function Write-VersionFile {
    param(
        [Parameter(Mandatory)][string]$SemVer,
        [string]$LiteralPath = $version
    )
    $lines = readVerFile -LiteralPath $LiteralPath
    writeVerFile -SemVer $SemVer -Tag $lines[1] -Build $lines[2]
}

$verLines = readVerFile
$versionContents = $verLines[0]
$versionTagContents = if (Test-Path -LiteralPath $versionTag) { ([IO.File]::ReadAllText($versionTag)).Trim() } else { $verLines[1] }
$tag = if ($versionTagContents) { $versionTagContents } else { "v$versionContents" }

$buildNotesContents = if (Test-Path -LiteralPath $buildNotes) {
    [System.IO.File]::ReadAllText($buildNotes).Trim()
} else { '' }

function setVerBuild {
    param([Parameter(Mandatory)][string]$Platform)
    writeFileNoBom -LiteralPath $versionBuild -Content ($Platform.Trim() + "`n")
}

function checkVerBuild {
    param([string]$Architecture)
    if (Test-Path -LiteralPath $versionBuild) { return }
    setVerBuild ($Architecture ?? 'x64')
}

function writeClearedLine {
    param([Parameter(Mandatory)][string]$Text, [Parameter(Mandatory)][int]$PadWidth, [switch]$NoNewline)
    Write-Host "`r$Text$(' ' * [Math]::Max(0, $PadWidth - $Text.Length))" -NoNewline:$NoNewline
}

function closeOut {
    param([int]$Seconds = 5)
    $caller = $MyInvocation.PSCommandPath
    if ([string]::IsNullOrWhiteSpace($caller)) { return }
    $argv = [Environment]::GetCommandLineArgs()
    $fileArg = $null
    for ($i = 0; $i -lt $argv.Length; $i++) {
        if ("$($argv[$i])" -match '^(?i)-File$|^(?i)-f$') {
            if ($i + 1 -lt $argv.Length) { $fileArg = $argv[$i + 1] }
            break
        }
    }
    if ([string]::IsNullOrWhiteSpace($fileArg)) { return }
    try {
        $fileFull = [IO.Path]::GetFullPath($fileArg)
        $callerFull = [IO.Path]::GetFullPath($caller)
    } catch { return }
    if (-not [string]::Equals($fileFull, $callerFull, [StringComparison]::OrdinalIgnoreCase)) { return }
    if ($Seconds -lt 0) { $Seconds = 0 }
    if ($Seconds -gt 0) {
        $pad = "closing after $Seconds seconds..."
        foreach ($n in $Seconds..1) {
            writeClearedLine -Text "closing after $n seconds..." -PadWidth $pad.Length -NoNewline
            Start-Sleep -Seconds 1
        }
        writeClearedLine -Text 'closing...' -PadWidth $pad.Length
    }
    try {
        if ($env:SCRIPT_OWN_PANE -and $env:WEZTERM_PANE -and $weztermExe) {
            & $weztermExe @('cli', 'kill-pane', '--pane-id', $env:WEZTERM_PANE)
        }
    } catch { }
    [Environment]::Exit(0)
}

function runNativeCommand {
    param([Parameter(Mandatory)][string]$FilePath, [Parameter(Mandatory)]$ArgumentList, [Parameter(Mandatory)][string]$Name)
    & $FilePath @ArgumentList
    if ($LASTEXITCODE) { throw "$Name failed (exit $LASTEXITCODE)." }
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

function deleteDir {
    param([Parameter(Mandatory)][string]$Path)
    if (Test-Path -LiteralPath $Path) { Remove-Item -LiteralPath $Path -Recurse -Force }
}

function getArchitecture {
    param([string[]]$FlagArgs)
    $found = @()
    foreach ($arg in @($FlagArgs)) {
        if ([string]::IsNullOrWhiteSpace("$arg")) { continue }
        switch -Regex ("$arg".Trim()) {
            '(?i)^(x86|--x86|-x86|--86|-86)$' { throw 'ghostwriter++ release builds are x64 only.' }
            '(?i)^(x64|--x64|-x64|--64|-64)$' { $found += 'x64' }
            '(?i)^(arm64|--arm64|-arm64|--arm|-arm)$' { throw 'ghostwriter++ release builds are x64 only.' }
            '(?i)^(--help|-h)$' { return 'help' }
            default { throw "Unknown architecture flag: $arg" }
        }
    }
    $unique = @($found | Select-Object -Unique)
    if ($unique.Count -gt 1) { throw "Conflicting architecture flags: $($unique -join ', ')" }
    if ($unique.Count -eq 1) { return $unique[0] }
    $null
}

function Get-BuildDirectoryNameForPreset {
    param([Parameter(Mandatory)][string]$Preset)
    $dir = $script:GwPresetBuildDirs[$Preset]
    if (-not $dir) { throw "Unknown CMake preset (no build dir mapping): $Preset" }
    return $dir
}

function Get-CMakeProjectRoot {
    param(
        [Parameter(Mandatory)][string]$ScriptsDirectory,
        [string]$RepoRoot = '',
        [switch]$UseWorkingDirectory
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

function stagePublishAssets {
    New-Item -ItemType Directory -Path $publishFolder -Force | Out-Null
    Get-ChildItem -LiteralPath $publishFolder -File -ErrorAction SilentlyContinue | Remove-Item -Force
    $buildDir = Join-Path $repoRoot (Get-BuildDirectoryNameForPreset -Preset 'release')
    $internalZip = Join-Path $buildDir "ghostwriter++_v${versionContents}_win64.zip"
    $installerSrc = Join-Path $installerOutput 'ghostwriterpp-x64-installer.exe'
    $portableDest = Join-Path $publishFolder "ghostwriter++Portable_${tag}_win64.zip"
    $installerDest = Join-Path $publishFolder "ghostwriter++Installer_${tag}_win64.exe"
    if (-not (Test-Path -LiteralPath $internalZip)) { throw "Missing portable zip (run buildUpdater.ps1): $internalZip" }
    if (-not (Test-Path -LiteralPath $installerSrc)) { throw "Missing installer (run buildInstaller.ps1): $installerSrc" }
    Copy-Item -LiteralPath $internalZip -Destination $portableDest -Force
    Copy-Item -LiteralPath $installerSrc -Destination $installerDest -Force
}

function buildAll {
    param([string]$Architecture)
    runNativeCommand -FilePath "$PSScriptRoot\build.ps1" -ArgumentList @{ Preset = 'release' } -Name 'build.ps1'
    runNativeCommand -FilePath "$PSScriptRoot\buildUpdater.ps1" -ArgumentList @() -Name 'buildUpdater.ps1'
    runNativeCommand -FilePath "$PSScriptRoot\buildInstaller.ps1" -ArgumentList @() -Name 'buildInstaller.ps1'
    stagePublishAssets
    runNativeCommand -FilePath "$PSScriptRoot\updateReadme.ps1" -ArgumentList @() -Name 'updateReadme.ps1'
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
            $msg = 'Qt6Core.dll not on PATH. Source Craft (craftenv.ps1), set QTDIR, or add Qt 6 bin to PATH.'
            if ($Strict) { throw $msg }
            Write-Warning 'Sourced craftenv.ps1 but Qt6Core.dll still not on PATH. The app may fail with missing DLL errors.'
        }
        return
    }
    if ($Strict) {
        throw 'Qt6Core.dll not on PATH. Source Craft (craftenv.ps1), set QTDIR, or add Qt 6 bin to PATH.'
    }
    Write-Warning "Qt6Core.dll not on PATH and craftenv.ps1 not found at $craftEnv. The app may fail with missing DLL errors."
}

Set-Location -LiteralPath $repoRoot
