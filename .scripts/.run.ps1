param(
    [ValidateSet("dev", "release", "asan", "unity", "profile", "clazy", "dev-disable-deprecated")][string]$Preset = "release",
    [Alias("c")][switch]$Clean,
    [switch]$NoBuild,
    [switch]$Log,
    [switch]$DebugLog,
    [string]$CraftRoot = "C:\CraftRoot",
    [switch]$StrictExeIcon,
    [string]$RepoRoot = "",
    [Alias("cwd")][switch]$UseWorkingDirectory,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$AppArgs
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "resolveRepoRoot.ps1")
. (Join-Path $PSScriptRoot "scriptHelper.ps1")

function Get-GhostwriterDebugLogFromLastMarker([Parameter(Mandatory)][string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    if ((Get-Item -LiteralPath $Path).Length -eq 0) { return "" }
    $raw = Get-Content -LiteralPath $Path -Raw -Encoding utf8
    $marker = "--- ghostwriter++ log "
    $idx = $raw.LastIndexOf($marker, [StringComparison]::Ordinal)
    if ($idx -lt 0) { return $raw.TrimEnd() }
    $raw.Substring($idx).TrimEnd()
}

# ghostwriter++.exe = CMake OUTPUT_NAME (ghostwriterpp, src/CMakeLists.txt).
$ExeFileName = "ghostwriter++.exe"
$repoRoot = Get-CMakeProjectRoot -ScriptsDirectory $PSScriptRoot -RepoRoot $RepoRoot -UseWorkingDirectory:$UseWorkingDirectory
Write-Host "Repository root: $repoRoot"
$d = Get-BuildDirectoryNameForPreset -Preset $Preset
$exePath = Join-Path $repoRoot "$d/bin/$ExeFileName"
$buildScript = Join-Path $PSScriptRoot "build.ps1"
$debugLogPath = Join-Path ([IO.Path]::GetTempPath()) "ghostwriter++_last_run.log"

Get-Process -Name "ghostwriter++" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

Push-Location $repoRoot
try {
    if (-not $NoBuild) {
        & $buildScript -Preset $Preset -Clean:$Clean -CraftRoot $CraftRoot -StrictExeIcon:$StrictExeIcon -RepoRoot $repoRoot
    }
    if (-not (Test-Path -LiteralPath $exePath)) { throw "Executable not found: $exePath" }

    Ensure-QtRuntimeFromCraft -ProjectRoot $repoRoot -CraftRoot $CraftRoot
    Write-Host "Running: $exePath"

    $app = if ($AppArgs) { @($AppArgs) } else { @() }
    if ($DebugLog) {
        $app = @('--debug-log') + $app
        Write-Host "Qt log mirror: $debugLogPath (--debug-log)"
    }

    if ($Log) {
        $stdoutLog = Join-Path $repoRoot "gw_stdout.log"
        $stderrLog = Join-Path $repoRoot "gw_stderr.log"
        Write-Host "Logging stdout -> $stdoutLog"
        Write-Host "Logging stderr -> $stderrLog"
        $sp = @{
            FilePath = $exePath; NoNewWindow = $true; Wait = $true; PassThru = $true
            RedirectStandardOutput = $stdoutLog; RedirectStandardError = $stderrLog
        }
        if ($app) { $sp.ArgumentList = $app }
        $exit = (Start-Process @sp).ExitCode
        foreach ($pair in @(@('stdout', $stdoutLog), @('stderr', $stderrLog))) {
            $path = $pair[1]
            if ((Test-Path -LiteralPath $path) -and ((Get-Item -LiteralPath $path).Length -gt 0)) {
                Write-Host "--- $($pair[0]) ---"
                Get-Content -LiteralPath $path -Raw | Write-Host
            }
        }
    }
    else {
        & $exePath @app
        $exit = $LASTEXITCODE
    }

    Write-Host "$ExeFileName exited with code $exit (0x$([Convert]::ToString($exit, 16)))"

    if ($DebugLog) {
        Write-Host "--- Log from last '--- ghostwriter++ log' marker: $debugLogPath ---"
        $tail = Get-GhostwriterDebugLogFromLastMarker -Path $debugLogPath
        if ($null -eq $tail) { Write-Host "(log file not found)" }
        elseif ($tail -eq "") { Write-Host "(log file empty)" }
        else { Write-Host $tail }
    }

    if ($exit -ne 0) { exit $exit }
}
finally { Pop-Location }
