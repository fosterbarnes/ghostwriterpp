param(
    [ValidateSet("dev", "release", "asan", "unity", "profile", "clazy", "dev-disable-deprecated")][string]$Preset = "release",
    [Alias("c")][switch]$Clean,
    [string]$CraftRoot = "C:\CraftRoot",
    [switch]$StrictExeIcon,
    [string]$RepoRoot = "",
    [Alias("cwd")][switch]$UseWorkingDirectory
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "resolveRepoRoot.ps1")
. (Join-Path $PSScriptRoot "scriptHelper.ps1")

function Remove-PathWithRetry([Parameter(Mandatory)][string]$Path, [switch]$Recurse, [int]$MaxAttempts = 15, [int]$DelayMs = 350) {
    if (-not (Test-Path -LiteralPath $Path)) { return }
    $last = $null
    for ($i = 1; $i -le $MaxAttempts; $i++) {
        try {
            if (Test-Path -LiteralPath $Path -PathType Leaf) {
                Set-ItemProperty -LiteralPath $Path -Name IsReadOnly -Value $false -ErrorAction SilentlyContinue
            }
            elseif ($Recurse -and (Test-Path -LiteralPath $Path -PathType Container)) {
                Get-ChildItem -LiteralPath $Path -Recurse -Force -ErrorAction SilentlyContinue | ForEach-Object { $_.Attributes = 'Normal' }
            }
            Remove-Item -LiteralPath $Path -Recurse:$Recurse -Force -ErrorAction Stop
            return
        }
        catch {
            $last = $_
            if ($i -lt $MaxAttempts) {
                Write-Warning "Waiting to remove locked path (attempt $i/$MaxAttempts): $Path"
                Start-Sleep -Milliseconds $DelayMs
            }
        }
    }
    throw "Could not remove: $Path. Close IDE/CMake/AV or delete manually. $($last.Exception.Message)"
}

function Repair-StaleCMakeCacheIfNeeded([Parameter(Mandatory)][string]$BuildDirPath, [Parameter(Mandatory)][string]$ExpectedSourceRoot) {
    $cacheFile = Join-Path $BuildDirPath "CMakeCache.txt"
    if (-not (Test-Path -LiteralPath $cacheFile)) { return }
    $hit = Select-String -LiteralPath $cacheFile -Pattern '^CMAKE_HOME_DIRECTORY:' | Select-Object -First 1
    if (-not $hit) { return }
    $eq = $hit.Line.IndexOf('=')
    if ($eq -lt 0) { return }
    $cachedRaw = $hit.Line.Substring($eq + 1).Trim()
    if ([string]::IsNullOrWhiteSpace($cachedRaw)) { return }
    $expected = [IO.Path]::GetFullPath($ExpectedSourceRoot)
    $cached = [IO.Path]::GetFullPath($cachedRaw)
    if ($cached.Equals($expected, [StringComparison]::OrdinalIgnoreCase)) { return }
    Write-Warning "CMake cache points at other source (cache: $cached, repo: $expected). Removing CMakeCache.txt and CMakeFiles."
    Remove-PathWithRetry -Path $cacheFile
    $cmakeFiles = Join-Path $BuildDirPath "CMakeFiles"
    if (Test-Path -LiteralPath $cmakeFiles) { Remove-PathWithRetry -Path $cmakeFiles -Recurse }
}

function Test-HasWindowsSdkLib {
    if (-not $env:LIB) { return $false }
    foreach ($p in $env:LIB.Split([IO.Path]::PathSeparator)) {
        if ($p -and (Test-Path (Join-Path $p 'kernel32.lib'))) { return $true }
    }
    $false
}

function Initialize-BuildEnvironment([Parameter(Mandatory)][string]$ProjectRoot) {
    if (Test-HasWindowsSdkLib) {
        Write-Host "Build environment OK (kernel32.lib visible on LIB.)"
        return
    }
    $craftEnv = Join-Path $CraftRoot 'craft\craftenv.ps1'
    if (-not (Test-Path $craftEnv)) {
        throw "kernel32.lib not on LIB and craftenv.ps1 missing: $craftEnv`nUse VS Developer PowerShell or -CraftRoot."
    }
    Write-Host "Sourcing Craft environment: $craftEnv"
    . $craftEnv
    if (-not (Test-HasWindowsSdkLib)) { throw "After craftenv.ps1, kernel32.lib still not on LIB." }
    Set-Location $ProjectRoot
}

# ghostwriter++.exe = CMake OUTPUT_NAME (ghostwriterpp, src/CMakeLists.txt).
$ExeFileName = "ghostwriter++.exe"
$repoRoot = Get-CMakeProjectRoot -ScriptsDirectory $PSScriptRoot -RepoRoot $RepoRoot -UseWorkingDirectory:$UseWorkingDirectory
Write-Host "Repository root: $repoRoot"
$buildDirPath = Join-Path $repoRoot (Get-BuildDirectoryNameForPreset -Preset $Preset)

Write-Host "Preset: $Preset"
if ($Clean) {
    Write-Host "Cleaning: $buildDirPath"
    Remove-PathWithRetry -Path $buildDirPath -Recurse
}

Push-Location $repoRoot
try {
    Initialize-BuildEnvironment -ProjectRoot $repoRoot
    if (-not $Clean) { Repair-StaleCMakeCacheIfNeeded -BuildDirPath $buildDirPath -ExpectedSourceRoot $repoRoot }

    $cfg = @('--preset', $Preset, '-S', $repoRoot)
    if ($Clean) { $cfg += '--fresh' }
    Write-Host "Configuring..."
    Invoke-NativeCommand -What "CMake configure" -FilePath cmake -ArgumentList $cfg
    Write-Host "Building..."
    Invoke-NativeCommand -What "CMake build" -FilePath cmake -ArgumentList @('--build', $buildDirPath)

    $exe = Join-Path $buildDirPath "bin/$ExeFileName"
    $ico = Join-Path $repoRoot "resources/icons/sc-apps-ghostwriter.ico"
    $rts = Join-Path $PSScriptRoot "applyIcons.rts"
    if ([System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([System.Runtime.InteropServices.OSPlatform]::Windows) -and
        (Test-Path -LiteralPath $exe) -and (Test-Path -LiteralPath $rts)) {
        $rtc = $null
        foreach ($c in @(
                $env:RTC_EXE
                (Get-Command rtc.exe -ErrorAction SilentlyContinue)?.Source
                (Join-Path ${env:ProgramFiles} "Heaventools\Resource Tuner Console\rtc.exe")
                (Join-Path ${env:ProgramFiles(x86)} "Heaventools\Resource Tuner Console\rtc.exe")
            )) {
            if ($c -and (Test-Path -LiteralPath $c)) { $rtc = (Resolve-Path -LiteralPath $c).Path; break }
        }
        if (-not $rtc) {
            $w = "rtc.exe not found; skipping EXE icon. Set RTC_EXE or install Resource Tuner Console."
            if ($StrictExeIcon) { throw $w }
            Write-Warning $w
        }
        else {
            if (-not (Test-Path -LiteralPath $ico)) { throw "Icon not found: $ico" }
            $exeR = (Resolve-Path -LiteralPath $exe).Path
            $icoR = (Resolve-Path -LiteralPath $ico).Path
            $rtsR = (Resolve-Path -LiteralPath $rts).Path
            Write-Host "Applying icon: $icoR -> $exeR"
            & $rtc /S /E /F:"$rtsR" /plhd01="$exeR" /plhd02="$icoR"
            if ($LASTEXITCODE -ne 0) { throw "rtc.exe failed ($LASTEXITCODE) /F:`"$rtsR`"" }
        }
    }

    Write-Host "Build complete."
}
finally {
    Pop-Location
}
