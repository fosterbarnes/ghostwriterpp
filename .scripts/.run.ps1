#requires -Version 7.0
param(
    [ValidateSet("dev", "release", "asan", "unity", "profile", "clazy", "dev-disable-deprecated")][string]$Preset = "release",
    [Alias('h')][switch]$Help,
    [Alias("c")][switch]$Clean,
    [switch]$NoBuild,
    [switch]$Log,
    [switch]$DebugLog,
    # Accepted for muscle memory (musicApp-style). Craft/Windows builds are x64-only.
    [switch]$X64,
    [string]$CraftRoot = "C:\CraftRoot",
    [switch]$StrictExeIcon,
    [string]$RepoRoot = "",
    [Alias("cwd")][switch]$UseWorkingDirectory,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$AppArgs
)
$ErrorActionPreference = 'Stop'
if ($Help) {
    Write-Host '.run.ps1 [-Preset release|dev|...] [-Clean] [-NoBuild] [-Log] [-DebugLog] [-- app args...]'
    return
}
. "$PSScriptRoot\scriptHelper.ps1"

# ghostwriter++.exe = CMake OUTPUT_NAME (ghostwriterpp, src/CMakeLists.txt).
$ExeFileName = "ghostwriter++.exe"
$repoRoot = Get-CMakeProjectRoot -ScriptsDirectory $PSScriptRoot -RepoRoot $RepoRoot -UseWorkingDirectory:$UseWorkingDirectory
Set-Location -LiteralPath $repoRoot
Write-Host "Repository root: $repoRoot"
$appArgList = [System.Collections.Generic.List[string]]::new()
foreach ($a in @($AppArgs)) {
    if ($null -eq $a -or [string]::IsNullOrWhiteSpace([string]$a)) { continue }
    $t = ([string]$a).Trim()
    switch -Regex ($t) {
        '^(?i)(--x64|--64|-x64|-64)$' { $X64 = $true; continue }
        '^(?i)(--x86|--86|-x86|-86|--arm64|--arm|-arm64|-arm|--portable|--p|-portable|-p)$' {
            throw "ghostwriter++ Windows builds are x64 only (Craft ABI windows-cl-msvc2022-x86_64). Unsupported: $t"
        }
        default { $appArgList.Add($a) }
    }
}
$AppArgs = $appArgList.ToArray()
if ($X64) { Write-Host "Architecture: x64" }

function Get-GhostwriterDebugLogFromLastMarker([Parameter(Mandatory)][string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    if ((Get-Item -LiteralPath $Path).Length -eq 0) { return "" }
    $raw = Get-Content -LiteralPath $Path -Raw -Encoding utf8
    $marker = "--- ghostwriter++ log "
    $idx = $raw.LastIndexOf($marker, [StringComparison]::Ordinal)
    if ($idx -lt 0) { return $raw.TrimEnd() }
    $raw.Substring($idx).TrimEnd()
}

function Stop-GhostwriterApp {
    Get-Process -Name "ghostwriter++" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
}

$d = Get-BuildDirectoryNameForPreset -Preset $Preset
$exePath = Join-Path $repoRoot "$d/bin/$ExeFileName"
$buildScript = Join-Path $PSScriptRoot "build.ps1"
$debugLogPath = Join-Path ([IO.Path]::GetTempPath()) "ghostwriter++_last_run.log"

Stop-GhostwriterApp

Push-Location $repoRoot
try {
    $keepRunning = $true
    $isFirstLaunch = $true

    while ($keepRunning) {
        if (-not $NoBuild) {
            $cleanThis = $Clean -and $isFirstLaunch
            & $buildScript -Preset $Preset -Clean:$cleanThis -X64:$X64 -CraftRoot $CraftRoot -StrictExeIcon:$StrictExeIcon -RepoRoot $repoRoot
        }
        if (-not (Test-Path -LiteralPath $exePath)) { throw "Executable not found: $exePath" }

        Ensure-QtRuntimeFromCraft -ProjectRoot $repoRoot -CraftRoot $CraftRoot
        Write-Host "Running: $exePath"

        $app = if ($AppArgs -and $AppArgs.Count -gt 0) { @($AppArgs) } else { @() }
        if ($DebugLog) {
            $app = @('--debug-log') + $app
            Write-Host "Qt log mirror: $debugLogPath (--debug-log)"
        }

        $sp = @{
            FilePath     = $exePath
            NoNewWindow  = $true
            PassThru     = $true
        }
        if ($app) { $sp.ArgumentList = $app }

        if ($Log) {
            $stdoutLog = Join-Path $repoRoot "gw_stdout.log"
            $stderrLog = Join-Path $repoRoot "gw_stderr.log"
            Write-Host "Logging stdout -> $stdoutLog"
            Write-Host "Logging stderr -> $stderrLog"
            $sp.RedirectStandardOutput = $stdoutLog
            $sp.RedirectStandardError = $stderrLog
        }

        $proc = Start-Process @sp
        $isFirstLaunch = $false
        Write-Host "ghostwriter++ is running. Type 'q' to stop, 'r' or UpArrow to restart."

        $restartRequested = $false
        $lineBuffer = ''

        while (-not $proc.HasExited) {
            Start-Sleep -Milliseconds 50
            try {
                if (-not [Console]::KeyAvailable) { continue }
            }
            catch {
                continue
            }

            $key = [Console]::ReadKey($true)
            if ($key.Key -eq [ConsoleKey]::UpArrow) {
                Write-Host 'Restarting ghostwriter++...'
                Stop-GhostwriterApp
                $restartRequested = $true
                break
            }
            if ($key.Key -eq [ConsoleKey]::Enter) {
                Write-Host ''
                $userInput = $lineBuffer.Trim()
                $lineBuffer = ''
                if ($userInput -in @('q', 'quit', 'exit')) {
                    Write-Host 'Stopping ghostwriter++ and exiting script...'
                    Stop-GhostwriterApp
                    $keepRunning = $false
                    break
                }
                if ($userInput -in @('r', 'restart')) {
                    Write-Host 'Restarting ghostwriter++...'
                    Stop-GhostwriterApp
                    $restartRequested = $true
                    break
                }
                continue
            }
            if ($key.Key -eq [ConsoleKey]::Backspace) {
                if ($lineBuffer.Length -gt 0) {
                    $lineBuffer = $lineBuffer.Substring(0, $lineBuffer.Length - 1)
                    Write-Host "`b `b" -NoNewline
                }
                continue
            }
            if ($key.KeyChar -and -not [char]::IsControl($key.KeyChar)) {
                $lineBuffer += $key.KeyChar
                Write-Host -NoNewline $key.KeyChar
            }
        }

        if (-not $proc.HasExited) {
            try { $proc.WaitForExit(5000) | Out-Null } catch { }
        }

        $exit = $null
        try { $exit = $proc.ExitCode } catch { }

        if ($proc.HasExited) {
            if ($null -ne $exit) {
                Write-Host "$ExeFileName exited with code $exit (0x$([Convert]::ToString($exit, 16)))"
            }
            else {
                Write-Host "$ExeFileName stopped."
            }
        }

        if ($Log) {
            foreach ($pair in @(@('stdout', $stdoutLog), @('stderr', $stderrLog))) {
                $path = $pair[1]
                if ((Test-Path -LiteralPath $path) -and ((Get-Item -LiteralPath $path).Length -gt 0)) {
                    Write-Host "--- $($pair[0]) ---"
                    Get-Content -LiteralPath $path -Raw | Write-Host
                }
            }
        }

        if ($DebugLog) {
            Write-Host "--- Log from last '--- ghostwriter++ log' marker: $debugLogPath ---"
            $tail = Get-GhostwriterDebugLogFromLastMarker -Path $debugLogPath
            if ($null -eq $tail) { Write-Host "(log file not found)" }
            elseif ($tail -eq "") { Write-Host "(log file empty)" }
            else { Write-Host $tail }
        }

        if (-not $keepRunning -or -not $restartRequested) { break }
    }

    if ($null -ne $exit -and $exit -ne 0) { exit $exit }
}
finally { Pop-Location }
closeOut 0
