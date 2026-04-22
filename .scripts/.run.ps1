param(
    [ValidateSet("dev", "release", "asan", "unity", "profile", "clazy", "dev-disable-deprecated")]
    [string]$Preset = "release",
    [Alias("c")]
    [switch]$Clean,
    [switch]$NoBuild,
    [switch]$Log,
    [switch]$DebugLog,
    [string]$CraftRoot = "C:\CraftRoot",
    [switch]$StrictExeIcon,
    [string]$RepoRoot = "",
    [Alias("cwd")]
    [switch]$UseWorkingDirectory,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$AppArgs
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "Resolve-RepoRoot.ps1")

# Must match CMake OUTPUT_NAME for target ghostwriterpp (src/CMakeLists.txt).
$ExeFileName = "ghostwriter++.exe"

$repoRoot = Get-CMakeProjectRoot -ScriptsDirectory $PSScriptRoot -RepoRoot $RepoRoot -UseWorkingDirectory:$UseWorkingDirectory
Write-Host "Repository root: $repoRoot"

$buildDirByPreset = @{
    "dev"                    = "build"
    "release"                = "build-release"
    "asan"                   = "build-asan"
    "unity"                  = "build-unity"
    "profile"                = "build-profile"
    "clazy"                  = "build-clazy"
    "dev-disable-deprecated" = "build-disable-deprecated"
}

$buildDirName = $buildDirByPreset[$Preset]
$exePath = Join-Path $repoRoot "$buildDirName/bin/$ExeFileName"
$buildScript = Join-Path $PSScriptRoot ".build.ps1"

function Test-HasQtOnPath {
    if (-not $env:PATH) { return $false }
    foreach ($p in $env:PATH.Split([IO.Path]::PathSeparator)) {
        if ([string]::IsNullOrWhiteSpace($p)) { continue }
        if (Test-Path (Join-Path $p 'Qt6Core.dll')) { return $true }
    }
    return $false
}

function Get-GhostwriterDebugLogFromLastMarker {
    param(
        [Parameter(Mandatory)] [string]$Path
    )
    if (-not (Test-Path -LiteralPath $Path)) {
        return $null
    }
    $item = Get-Item -LiteralPath $Path
    if ($item.Length -eq 0) {
        return ""
    }
    $raw = Get-Content -LiteralPath $Path -Raw -Encoding utf8
    $marker = "--- ghostwriter++ log "
    $idx = $raw.LastIndexOf($marker, [StringComparison]::Ordinal)
    if ($idx -lt 0) {
        return $raw.TrimEnd()
    }
    return $raw.Substring($idx).TrimEnd()
}

function Initialize-RunEnvironment {
    if (Test-HasQtOnPath) {
        return
    }

    $craftEnv = Join-Path $CraftRoot 'craft\craftenv.ps1'
    if (Test-Path $craftEnv) {
        Write-Host "Sourcing Craft environment: $craftEnv"
        . $craftEnv
        if (-not (Test-HasQtOnPath)) {
            Write-Warning "Sourced craftenv.ps1 but Qt6Core.dll still not on PATH. The app may fail with missing DLL errors."
        }
        return
    }

    Write-Warning "Qt6Core.dll not on PATH and craftenv.ps1 not found at $craftEnv. The app may fail with missing DLL errors."
}

Get-Process -Name "ghostwriter++" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

Push-Location $repoRoot
try {
    if (-not $NoBuild) {
        & $buildScript -Preset $Preset -Clean:$Clean -CraftRoot $CraftRoot -StrictExeIcon:$StrictExeIcon -RepoRoot $repoRoot
    }

    if (-not (Test-Path $exePath)) {
        throw "Executable not found: $exePath"
    }


    Initialize-RunEnvironment
    Write-Host "Running: $exePath"

    $debugLogPath = Join-Path ([System.IO.Path]::GetTempPath()) "ghostwriter++_last_run.log"
    if ($DebugLog) {
        if (-not $AppArgs) { $AppArgs = @() }
        $AppArgs = @('--debug-log') + @($AppArgs)
        Write-Host "Qt log mirror: $debugLogPath (--debug-log)"
    }

    if ($Log) {
        # /SUBSYSTEM:WINDOWS detaches stdout/stderr from the parent console, so
        # normal `& $exePath` eats any qWarning/qDebug output. Route it through
        # Start-Process with explicit redirection so we can see it.
        $stdoutLog = Join-Path $repoRoot "gw_stdout.log"
        $stderrLog = Join-Path $repoRoot "gw_stderr.log"
        Write-Host "Logging stdout -> $stdoutLog"
        Write-Host "Logging stderr -> $stderrLog"

        $procArgs = @{
            FilePath = $exePath
            NoNewWindow = $true
            Wait = $true
            PassThru = $true
            RedirectStandardOutput = $stdoutLog
            RedirectStandardError = $stderrLog
        }
        if ($AppArgs) { $procArgs.ArgumentList = $AppArgs }

        $proc = Start-Process @procArgs
        $exit = $proc.ExitCode

        foreach ($pair in @(@('stdout', $stdoutLog), @('stderr', $stderrLog))) {
            $name = $pair[0]
            $path = $pair[1]
            if ((Test-Path $path) -and ((Get-Item $path).Length -gt 0)) {
                Write-Host "--- $name ---"
                Get-Content -Raw $path | Write-Host
            }
        }
    }
    else {
        & $exePath @AppArgs
        $exit = $LASTEXITCODE
    }

    Write-Host "$ExeFileName exited with code $exit (0x$([Convert]::ToString($exit, 16)))"

    if ($DebugLog) {
        Write-Host "--- Log from last '--- ghostwriter++ log' marker: $debugLogPath ---"
        $tail = Get-GhostwriterDebugLogFromLastMarker -Path $debugLogPath
        if ($null -eq $tail) {
            Write-Host "(log file not found)"
        }
        elseif ($tail -eq "") {
            Write-Host "(log file empty)"
        }
        else {
            Write-Host $tail
        }
    }

    if ($exit -ne 0) {
        exit $exit
    }
}
finally {
    Pop-Location
}
