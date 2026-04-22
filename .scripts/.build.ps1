param(
    [ValidateSet("dev", "release", "asan", "unity", "profile", "clazy", "dev-disable-deprecated")]
    [string]$Preset = "release",
    [Alias("c")]
    [switch]$Clean,
    [string]$CraftRoot = "C:\CraftRoot",
    [switch]$StrictExeIcon
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot

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
$buildDirPath = Join-Path $repoRoot $buildDirName

function Invoke-Native {
    param(
        [Parameter(Mandatory)] [string]$What,
        [Parameter(Mandatory, ValueFromRemainingArguments)] [string[]]$Cmd
    )
    & $Cmd[0] @($Cmd[1..($Cmd.Count - 1)])
    if ($LASTEXITCODE -ne 0) {
        throw "$What failed (exit $LASTEXITCODE): $($Cmd -join ' ')"
    }
}

function Test-HasWindowsSdkLib {
    if (-not $env:LIB) { return $false }
    foreach ($p in $env:LIB.Split([IO.Path]::PathSeparator)) {
        if ([string]::IsNullOrWhiteSpace($p)) { continue }
        if (Test-Path (Join-Path $p 'kernel32.lib')) { return $true }
    }
    return $false
}

function Get-ResourceTunerConsoleExe {
    if ($env:RTC_EXE -and (Test-Path -LiteralPath $env:RTC_EXE)) {
        return (Resolve-Path -LiteralPath $env:RTC_EXE).Path
    }
    $cmd = Get-Command rtc.exe -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }
    $candidates = @(
        (Join-Path ${env:ProgramFiles} "Heaventools\Resource Tuner Console\rtc.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "Heaventools\Resource Tuner Console\rtc.exe")
    )
    foreach ($c in $candidates) {
        if ($c -and (Test-Path -LiteralPath $c)) {
            return (Resolve-Path -LiteralPath $c).Path
        }
    }
    return $null
}

function Invoke-ApplyExeIcon {
    param(
        [Parameter(Mandatory)] [string]$ExePath,
        [Parameter(Mandatory)] [string]$IconPath,
        [Parameter(Mandatory)] [string]$RtsPath,
        [switch]$Strict
    )
    $rtc = Get-ResourceTunerConsoleExe
    if (-not $rtc) {
        $msg = @"
Resource Tuner Console (rtc.exe) not found; skipping EXE icon patch. Install RTC or set RTC_EXE to rtc.exe's full path.
Searched: PATH, `$env:RTC_EXE, Program Files\Heaventools\Resource Tuner Console\rtc.exe
"@
        if ($Strict) { throw $msg }
        Write-Warning $msg.TrimEnd()
        return
    }
    if (-not (Test-Path -LiteralPath $IconPath)) {
        throw "Icon file not found: $IconPath"
    }
    $exeResolved = (Resolve-Path -LiteralPath $ExePath).Path
    $icoResolved = (Resolve-Path -LiteralPath $IconPath).Path
    $rtsResolved = (Resolve-Path -LiteralPath $RtsPath).Path
    Write-Host "Applying icon via Resource Tuner Console: $icoResolved -> $exeResolved"
    & $rtc /S /E /F:"$rtsResolved" /plhd01="$exeResolved" /plhd02="$icoResolved"
    if ($LASTEXITCODE -ne 0) {
        throw "Resource Tuner Console failed (exit $LASTEXITCODE): rtc.exe /F:`"$rtsResolved`""
    }
}

function Initialize-BuildEnvironment {
    if (Test-HasWindowsSdkLib) {
        Write-Host "Build environment OK (kernel32.lib visible on LIB)."
        return
    }

    $craftEnv = Join-Path $CraftRoot 'craft\craftenv.ps1'
    if (Test-Path $craftEnv) {
        Write-Host "Sourcing Craft environment: $craftEnv"
        . $craftEnv
        if (-not (Test-HasWindowsSdkLib)) {
            throw "Sourced craftenv.ps1 but kernel32.lib still not on LIB. Run from a Craft/VS dev shell."
        }
        Set-Location $repoRoot
        return
    }

    throw @"
Build environment missing. kernel32.lib is not on LIB and craftenv.ps1 was not found at:
  $craftEnv
Run this script from a Craft dev shell, a 'Developer PowerShell for VS 2022' prompt,
or pass -CraftRoot <path> so craftenv.ps1 can be sourced automatically.
"@
}

Write-Host "Preset: $Preset"

if ($Clean) {
    Write-Host "Cleaning: $buildDirPath"
    if (Test-Path $buildDirPath) {
        Remove-Item -Recurse -Force $buildDirPath
    }
}

Push-Location $repoRoot
try {
    Initialize-BuildEnvironment
    Set-Location $repoRoot

    Write-Host "Configuring..."
    if ($Clean) {
        Invoke-Native "CMake configure" cmake --preset $Preset -S $repoRoot --fresh
    }
    else {
        Invoke-Native "CMake configure" cmake --preset $Preset -S $repoRoot
    }

    Write-Host "Building..."
    Invoke-Native "CMake build" cmake --build $buildDirPath

    $builtExe = Join-Path $buildDirPath "bin/ghostwriter.exe"
    $iconPath = Join-Path $repoRoot "resources/icons/sc-apps-ghostwriter.ico"
    $rtsPath = Join-Path $PSScriptRoot "apply-exe-icon.rts"
    $onWindows = [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([System.Runtime.InteropServices.OSPlatform]::Windows)
    if ($onWindows -and (Test-Path -LiteralPath $builtExe) -and (Test-Path -LiteralPath $rtsPath)) {
        Invoke-ApplyExeIcon -ExePath $builtExe -IconPath $iconPath -RtsPath $rtsPath -Strict:$StrictExeIcon
    }

    Write-Host "Build complete."
}
finally {
    Pop-Location
}
