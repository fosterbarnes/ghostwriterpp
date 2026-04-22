param(
    [Alias("c")][switch]$Clean,
    [switch]$NoBuild,
    [string]$CraftRoot = "C:\CraftRoot",
    [switch]$StrictExeIcon,
    [ValidateRange(0, 2)][int]$WinDeployVerbose = 0,
    [switch]$IncludeQtTranslations,
    [string]$RepoRoot = "",
    [Alias("cwd")][switch]$UseWorkingDirectory
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "resolveRepoRoot.ps1")
. (Join-Path $PSScriptRoot "scriptHelper.ps1")

function Get-QtInstallPrefix {
    foreach ($exe in @('qtpaths6.exe', 'qtpaths.exe')) {
        $cmd = Get-Command $exe -ErrorAction SilentlyContinue
        if (-not $cmd) { continue }
        foreach ($pair in @(@('--query', 'QT_INSTALL_PREFIX'), @('-query', 'QT_INSTALL_PREFIX'))) {
            $p = & $cmd.Source @pair 2>$null
            if ($LASTEXITCODE -eq 0 -and $p) { return $p.Trim() }
        }
    }
    $qmake = Get-Command qmake.exe -ErrorAction SilentlyContinue
    if ($qmake) {
        $p = & qmake.exe -query QT_INSTALL_PREFIX 2>$null
        if ($LASTEXITCODE -eq 0 -and $p) { return $p.Trim() }
    }
    $null
}

function Ensure-CraftRootQtResourcesForWinDeploy([Parameter(Mandatory)][string]$CraftRoot) {
    $expectedIcu = Join-Path $CraftRoot 'resources\icudtl.dat'
    if (Test-Path -LiteralPath $expectedIcu) { return }

    $craftBin = Join-Path $CraftRoot 'bin'
    $binIcu = Join-Path $craftBin 'icudtl.dat'
    if (Test-Path -LiteralPath $binIcu) {
        $destRes = Join-Path $CraftRoot 'resources'
        Write-Host "Staging WebEngine payloads Craft bin -> $destRes (windeployqt)"
        New-Item -ItemType Directory -Force -Path $destRes | Out-Null
        Copy-Item -LiteralPath $binIcu -Destination (Join-Path $destRes 'icudtl.dat') -Force
        Get-ChildItem -LiteralPath $craftBin -File -Filter 'qtwebengine*.pak' -ErrorAction SilentlyContinue |
            ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $destRes $_.Name) -Force }
        foreach ($n in @('v8_context_snapshot.bin', 'snapshot_blob.bin', 'resources.pak')) {
            $p = Join-Path $craftBin $n
            if (Test-Path -LiteralPath $p) { Copy-Item -LiteralPath $p -Destination (Join-Path $destRes $n) -Force }
        }
        if (-not (Test-Path -LiteralPath $expectedIcu)) { throw "Failed to stage $expectedIcu from Craft bin." }
        return
    }

    $srcRes = $null
    $prefix = Get-QtInstallPrefix
    if ($prefix) {
        $cand = Join-Path $prefix 'resources'
        if (Test-Path -LiteralPath (Join-Path $cand 'icudtl.dat')) { $srcRes = $cand }
    }
    if (-not $srcRes) {
        $libRoot = Join-Path $CraftRoot 'lib'
        if (Test-Path -LiteralPath $libRoot) {
            $hit = Get-ChildItem -LiteralPath $libRoot -Recurse -Filter 'icudtl.dat' -File -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($hit) { $srcRes = $hit.Directory.FullName }
        }
    }
    if (-not $srcRes) {
        Write-Warning "No icudtl.dat found to mirror into $(Join-Path $CraftRoot 'resources'). If windeployqt fails, source craftenv or copy Qt WebEngine resources manually."
        return
    }

    $destRes = Join-Path $CraftRoot 'resources'
    $srcFull = [IO.Path]::GetFullPath($srcRes)
    $destFull = [IO.Path]::GetFullPath($destRes)
    if ($srcFull -eq $destFull) { return }

    Write-Host "Staging Qt resources for windeployqt: $srcFull -> $destFull"
    New-Item -ItemType Directory -Force -Path $destFull | Out-Null
    robocopy $srcFull $destFull /E /XO /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy Qt resources failed (exit $LASTEXITCODE)" }
    if (-not (Test-Path -LiteralPath $expectedIcu)) { throw "After staging, still missing: $expectedIcu" }
}

function Get-WinDeployQtExe {
    foreach ($name in @('windeployqt6', 'windeployqt')) {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if ($cmd) { return $cmd.Source }
    }
    if ($env:QTDIR) {
        foreach ($leaf in @('windeployqt6.exe', 'windeployqt.exe')) {
            $p = Join-Path $env:QTDIR "bin\$leaf"
            if (Test-Path -LiteralPath $p) { return (Resolve-Path -LiteralPath $p).Path }
        }
    }
    throw "windeployqt not found on PATH or under QTDIR. Source Craft (see .run.ps1)."
}

function Get-DumpBinExe {
    $cmd = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) { return $null }
    $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    if (-not $installPath) { return $null }
    foreach ($msvc in (Get-ChildItem -Path (Join-Path $installPath "VC\Tools\MSVC") -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending)) {
        $db = Join-Path $msvc.FullName "bin\Hostx64\x64\dumpbin.exe"
        if (Test-Path -LiteralPath $db) { return $db }
    }
    $null
}

function Test-IsSystemDllPath([string]$FullPath) {
    if ([string]::IsNullOrWhiteSpace($FullPath)) { return $true }
    try {
        $full = [IO.Path]::GetFullPath($FullPath)
    }
    catch {
        return $true
    }
    $sys32 = [IO.Path]::GetFullPath((Join-Path $env:SystemRoot 'System32'))
    $sysWow = [IO.Path]::GetFullPath((Join-Path $env:SystemRoot 'SysWOW64'))
    foreach ($root in @($sys32, $sysWow)) {
        if ($full.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) { return $true }
    }
    $false
}

function Get-DumpBinDependents([Parameter(Mandatory)][string]$DumpBin, [Parameter(Mandatory)][string]$PePath) {
    $lines = & $DumpBin /dependents $PePath 2>&1 | ForEach-Object { $_.ToString() }
    if ($LASTEXITCODE -ne 0) { throw "dumpbin /dependents failed for $PePath (exit $LASTEXITCODE)" }
    $deps = New-Object 'System.Collections.Generic.List[string]'
    foreach ($line in $lines) {
        if ($line -match '^\s+(\S+\.dll)\s*$') { [void]$deps.Add($Matches[1]) }
    }
    return $deps
}

function Resolve-NonSystemDll([Parameter(Mandatory)][string]$DllName, [Parameter(Mandatory)][string[]]$SearchDirs) {
    foreach ($dir in $SearchDirs) {
        if ([string]::IsNullOrWhiteSpace($dir) -or -not (Test-Path -LiteralPath $dir)) { continue }
        $cand = Join-Path $dir $DllName
        if (Test-Path -LiteralPath $cand) {
            $full = [IO.Path]::GetFullPath($cand)
            if (-not (Test-IsSystemDllPath $full)) { return $full }
        }
    }
    $null
}

function Copy-CmarkDllsToBin([Parameter(Mandatory)][string]$BuildRoot, [Parameter(Mandatory)][string]$DestBin) {
    Get-ChildItem -Path $BuildRoot -Recurse -File -Filter 'cmark-gfm*.dll' -ErrorAction SilentlyContinue | ForEach-Object {
        $src = [IO.Path]::GetFullPath($_.FullName)
        $dest = [IO.Path]::GetFullPath((Join-Path $DestBin $_.Name))
        if ($src -ne $dest) {
            Copy-Item -LiteralPath $src -Destination $dest -Force
            Write-Host "  cmark: $($_.Name)"
        }
    }
}

function Invoke-ReleaseFolderCleanup([Parameter(Mandatory)][string]$BinDir, [switch]$QtTranslationsKept) {
    Get-ChildItem -LiteralPath $BinDir -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -in @('.pdb', '.ilk', '.exp', '.lib', '.ipdb') } |
        ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }
    if (-not $QtTranslationsKept) {
        $tr = Join-Path $BinDir 'translations'
        if (Test-Path -LiteralPath $tr) { Remove-Item -LiteralPath $tr -Recurse -Force }
    }
    foreach ($sub in @('sqldrivers', 'geoservices', 'sensors', 'position', 'scxmldaemon', 'assetimporters', 'texttospeech')) {
        $d = Join-Path $BinDir "plugins\$sub"
        if (Test-Path -LiteralPath $d) { Remove-Item -LiteralPath $d -Recurse -Force }
    }
}

function Copy-PeDependencyClosure([Parameter(Mandatory)][string]$DumpBin, [Parameter(Mandatory)][string]$BinDir, [Parameter(Mandatory)][string[]]$SeedPes) {
    $pathDirs = if ($env:PATH) { @($env:PATH.Split([IO.Path]::PathSeparator) | Where-Object { $_ }) } else { @() }
    $searchDirs = @($BinDir) + $pathDirs
    $queue = [System.Collections.Queue]::new()
    $scanned = @{}
    foreach ($s in $SeedPes) {
        if ($s -and (Test-Path -LiteralPath $s)) { $queue.Enqueue((Resolve-Path -LiteralPath $s).Path) }
    }
    while ($queue.Count -gt 0) {
        $pe = $queue.Dequeue()
        if ($scanned.ContainsKey($pe)) { continue }
        $scanned[$pe] = $true
        $deps = Get-DumpBinDependents -DumpBin $DumpBin -PePath $pe
        foreach ($dllName in $deps) {
            $destPath = Join-Path $BinDir $dllName
            if (Test-Path -LiteralPath $destPath) {
                $existing = (Resolve-Path -LiteralPath $destPath).Path
                if (-not $scanned.ContainsKey($existing)) { $queue.Enqueue($existing) }
                continue
            }
            $src = Resolve-NonSystemDll -DllName $dllName -SearchDirs $searchDirs
            if (-not $src) { continue }
            Copy-Item -LiteralPath $src -Destination $destPath -Force
            Write-Host "  dep: $dllName <- $src"
            $queue.Enqueue((Resolve-Path -LiteralPath $destPath).Path)
        }
    }
}

# ghostwriter++.exe = CMake OUTPUT_NAME (ghostwriterpp, src/CMakeLists.txt).
$ExeFileName = "ghostwriter++.exe"
$repoRoot = Get-CMakeProjectRoot -ScriptsDirectory $PSScriptRoot -RepoRoot $RepoRoot -UseWorkingDirectory:$UseWorkingDirectory
Write-Host "Repository root: $repoRoot"
$buildDirPath = Join-Path $repoRoot (Get-BuildDirectoryNameForPreset -Preset release)
$exePath = Join-Path $buildDirPath "bin/$ExeFileName"
$binDir = Split-Path -Parent $exePath
$buildScript = Join-Path $PSScriptRoot "build.ps1"

Push-Location $repoRoot
try {
    if (-not $NoBuild) {
        & $buildScript -Preset release -Clean:$Clean -CraftRoot $CraftRoot -StrictExeIcon:$StrictExeIcon -RepoRoot $repoRoot
    }
    if (-not (Test-Path -LiteralPath $exePath)) { throw "Executable not found: $exePath (build release first, or drop -NoBuild)" }

    Ensure-QtRuntimeFromCraft -ProjectRoot $repoRoot -CraftRoot $CraftRoot -Strict
    Ensure-CraftRootQtResourcesForWinDeploy -CraftRoot $CraftRoot

    Write-Host "Copying cmark DLLs into $binDir ..."
    Copy-CmarkDllsToBin -BuildRoot $buildDirPath -DestBin $binDir

    $windeployqt = Get-WinDeployQtExe
    Write-Host "Running windeployqt: $windeployqt"
    $wdArgs = @('--release', '--compiler-runtime', '--skip-plugin-types', 'qmltooling', '--verbose', "$WinDeployVerbose")
    if (-not $IncludeQtTranslations) { $wdArgs += '--no-translations' }
    $wdArgs += $exePath
    Invoke-NativeCommand -What "windeployqt" -FilePath $windeployqt -ArgumentList $wdArgs

    $dumpBin = Get-DumpBinExe
    if (-not $dumpBin) {
        Write-Warning "dumpbin.exe not found; skipping non-Qt dependency pass (KF6 DLLs may be missing elsewhere). Use VS C++ tools or a VS dev shell."
    }
    else {
        Write-Host "Resolving PE dependencies with: $dumpBin"
        $seeds = @($exePath)
        $webEngine = Join-Path $binDir 'QtWebEngineProcess.exe'
        if (Test-Path -LiteralPath $webEngine) { $seeds += $webEngine }
        Copy-PeDependencyClosure -DumpBin $dumpBin -BinDir $binDir -SeedPes $seeds
    }

    Write-Host "Pruning deploy folder (symbols, unused plugins; -IncludeQtTranslations keeps Qt .qm) ..."
    Invoke-ReleaseFolderCleanup -BinDir $binDir -QtTranslationsKept:$IncludeQtTranslations

    Write-Host "Release folder ready: $binDir"
    Write-Host "You can run $ExeFileName from Explorer or any shell without Craft on PATH."

    $releaseVer = Read-VersionFileFromScriptsRoot -ScriptsDirectory $PSScriptRoot
    $zipOut = Join-Path $buildDirPath "ghostwriter++_v${releaseVer}_win64.zip"
    if (Test-Path -LiteralPath $zipOut) { Remove-Item -LiteralPath $zipOut -Force }
    Write-Host "Zipping portable folder -> $zipOut (7z) ..."
    Push-Location $binDir
    try {
        $seven = Get-Command 7z -ErrorAction Stop
        Invoke-NativeCommand -What "7z" -FilePath $seven.Source -ArgumentList @('a', '-tzip', '-mx=5', $zipOut, '*')
    }
    finally { Pop-Location }
    Write-Host "Zip ready: $zipOut"

    $installerScript = Join-Path $PSScriptRoot "buildInstaller.ps1"
    Write-Host "Running: $installerScript"
    & $installerScript
}
finally {
    Pop-Location
}
