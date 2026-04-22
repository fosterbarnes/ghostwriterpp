param(
    [Alias("c")]
    [switch]$Clean,
    [switch]$NoBuild,
    [string]$CraftRoot = "C:\CraftRoot",
    [switch]$StrictExeIcon,
    [ValidateRange(0, 2)]
    [int]$WinDeployVerbose = 0,
    [switch]$IncludeQtTranslations
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDirPath = Join-Path $repoRoot "build-release"
$exePath = Join-Path $buildDirPath "bin/ghostwriter.exe"
$binDir = Split-Path -Parent $exePath
$buildScript = Join-Path $PSScriptRoot ".build.ps1"

function Test-HasQtOnPath {
    if (-not $env:PATH) { return $false }
    foreach ($p in $env:PATH.Split([IO.Path]::PathSeparator)) {
        if ([string]::IsNullOrWhiteSpace($p)) { continue }
        if (Test-Path (Join-Path $p 'Qt6Core.dll')) { return $true }
    }
    return $false
}

function Ensure-QtOnPath {
    if (Test-HasQtOnPath) { return }
    $craftEnv = Join-Path $CraftRoot 'craft\craftenv.ps1'
    if (Test-Path $craftEnv) {
        Write-Host "Sourcing Craft environment: $craftEnv"
        . $craftEnv
        Set-Location $repoRoot
    }
    if (-not (Test-HasQtOnPath)) {
        throw "Qt6Core.dll not on PATH. Source Craft (craftenv.ps1), set QTDIR, or add Qt 6 bin to PATH."
    }
}

function Get-QtInstallPrefix {
    foreach ($exe in @('qtpaths6.exe', 'qtpaths.exe')) {
        $cmd = Get-Command $exe -ErrorAction SilentlyContinue
        if (-not $cmd) { continue }
        foreach ($argpair in @(@('--query', 'QT_INSTALL_PREFIX'), @('-query', 'QT_INSTALL_PREFIX'))) {
            $p = & $cmd.Source @argpair 2>$null
            if ($LASTEXITCODE -eq 0 -and $p) { return $p.Trim() }
        }
    }
    $qmake = Get-Command qmake.exe -ErrorAction SilentlyContinue
    if ($qmake) {
        $p = & qmake.exe -query QT_INSTALL_PREFIX 2>$null
        if ($LASTEXITCODE -eq 0 -and $p) { return $p.Trim() }
    }
    return $null
}

function Ensure-CraftRootQtResourcesForWinDeploy {
    param([Parameter(Mandatory)] [string]$CraftRoot)
    $expectedIcu = Join-Path $CraftRoot 'resources\icudtl.dat'
    if (Test-Path -LiteralPath $expectedIcu) {
        return
    }

    # Craft merges Qt WebEngine data files into <CraftRoot>\bin; windeployqt still looks for
    # <CraftRoot>\resources\icudtl.dat (Qt prefix layout). Mirror the usual WebEngine payloads.
    $craftBin = Join-Path $CraftRoot 'bin'
    $binIcu = Join-Path $craftBin 'icudtl.dat'
    if (Test-Path -LiteralPath $binIcu) {
        $destRes = Join-Path $CraftRoot 'resources'
        Write-Host "Staging Qt WebEngine resources from Craft bin for windeployqt: $craftBin -> $destRes"
        New-Item -ItemType Directory -Force -Path $destRes | Out-Null
        Copy-Item -LiteralPath $binIcu -Destination (Join-Path $destRes 'icudtl.dat') -Force
        Get-ChildItem -LiteralPath $craftBin -File -Filter 'qtwebengine*.pak' -ErrorAction SilentlyContinue |
            ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $destRes $_.Name) -Force }
        foreach ($n in @('v8_context_snapshot.bin', 'snapshot_blob.bin', 'resources.pak')) {
            $p = Join-Path $craftBin $n
            if (Test-Path -LiteralPath $p) {
                Copy-Item -LiteralPath $p -Destination (Join-Path $destRes $n) -Force
            }
        }
        if (-not (Test-Path -LiteralPath $expectedIcu)) {
            throw "Failed to stage $expectedIcu from Craft bin."
        }
        return
    }

    $srcRes = $null
    $prefix = Get-QtInstallPrefix
    if ($prefix) {
        $cand = Join-Path $prefix 'resources'
        if (Test-Path -LiteralPath (Join-Path $cand 'icudtl.dat')) {
            $srcRes = $cand
        }
    }

    if (-not $srcRes) {
        $libRoot = Join-Path $CraftRoot 'lib'
        if (Test-Path -LiteralPath $libRoot) {
            $hit = Get-ChildItem -LiteralPath $libRoot -Recurse -Filter 'icudtl.dat' -File -ErrorAction SilentlyContinue |
                Select-Object -First 1
            if ($hit) {
                $srcRes = $hit.Directory.FullName
            }
        }
    }

    if (-not $srcRes) {
        Write-Warning @"
Could not find icudtl.dat to mirror into $(Join-Path $CraftRoot 'resources').
If windeployqt then errors on icudtl.dat, source craftenv.ps1 (qtpaths/qmake on PATH) or copy Qt WebEngine's resources folder there manually.
"@
        return
    }

    $destRes = Join-Path $CraftRoot 'resources'
    $srcFull = [IO.Path]::GetFullPath($srcRes)
    $destFull = [IO.Path]::GetFullPath($destRes)
    if ($srcFull -eq $destFull) {
        return
    }

    Write-Host "Staging Qt resources for windeployqt: $srcFull -> $destFull"
    New-Item -ItemType Directory -Force -Path $destFull | Out-Null
    robocopy $srcFull $destFull /E /XO /NFL /NDL /NJH /NJS /NP | Out-Null
    $rc = $LASTEXITCODE
    if ($rc -ge 8) {
        throw "robocopy Qt resources failed (exit $rc)"
    }
    if (-not (Test-Path -LiteralPath $expectedIcu)) {
        throw "After staging, still missing: $expectedIcu"
    }
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
    throw "windeployqt not found on PATH and not under QTDIR. Source Craft environment (see .run.ps1)."
}

function Get-DumpBinExe {
    $cmd = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) { return $null }
    $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    if (-not $installPath) { return $null }
    $candidates = Get-ChildItem -Path (Join-Path $installPath "VC\Tools\MSVC") -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending
    foreach ($msvc in $candidates) {
        $db = Join-Path $msvc.FullName "bin\Hostx64\x64\dumpbin.exe"
        if (Test-Path -LiteralPath $db) { return $db }
    }
    return $null
}

function Test-IsSystemDllPath {
    param([string]$FullPath)
    if ([string]::IsNullOrWhiteSpace($FullPath)) { return $true }
    try {
        $full = [IO.Path]::GetFullPath($FullPath)
    }
    catch { return $true }
    $sys32 = [IO.Path]::GetFullPath((Join-Path $env:SystemRoot 'System32'))
    $sysWow = [IO.Path]::GetFullPath((Join-Path $env:SystemRoot 'SysWOW64'))
    if ($full.StartsWith($sys32, [StringComparison]::OrdinalIgnoreCase)) { return $true }
    if ($full.StartsWith($sysWow, [StringComparison]::OrdinalIgnoreCase)) { return $true }
    return $false
}

function Get-DumpBinDependents {
    param(
        [Parameter(Mandatory)] [string]$DumpBin,
        [Parameter(Mandatory)] [string]$PePath
    )
    $lines = & $DumpBin /dependents $PePath 2>&1 | ForEach-Object { $_.ToString() }
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin /dependents failed for $PePath (exit $LASTEXITCODE)"
    }
    $deps = [System.Collections.Generic.List[string]]::new()
    foreach ($line in $lines) {
        if ($line -match '^\s+(\S+\.dll)\s*$') {
            $deps.Add($Matches[1])
        }
    }
    return $deps
}

function Resolve-NonSystemDll {
    param(
        [Parameter(Mandatory)] [string]$DllName,
        [Parameter(Mandatory)] [string[]]$SearchDirs
    )
    foreach ($dir in $SearchDirs) {
        if ([string]::IsNullOrWhiteSpace($dir)) { continue }
        if (-not (Test-Path -LiteralPath $dir)) { continue }
        $cand = Join-Path $dir $DllName
        if (Test-Path -LiteralPath $cand) {
            $full = [IO.Path]::GetFullPath($cand)
            if (-not (Test-IsSystemDllPath $full)) { return $full }
        }
    }
    return $null
}

function Copy-CmarkDllsToBin {
    param(
        [Parameter(Mandatory)] [string]$BuildRoot,
        [Parameter(Mandatory)] [string]$DestBin
    )
    Get-ChildItem -Path $BuildRoot -Recurse -File -Filter 'cmark-gfm*.dll' -ErrorAction SilentlyContinue |
        ForEach-Object {
            $src = [IO.Path]::GetFullPath($_.FullName)
            $dest = [IO.Path]::GetFullPath((Join-Path $DestBin $_.Name))
            if ($src -eq $dest) { return }
            Copy-Item -LiteralPath $src -Destination $dest -Force
            Write-Host "  cmark: $($_.Name)"
        }
}

function Invoke-ReleaseFolderCleanup {
    param(
        [Parameter(Mandatory)] [string]$BinDir,
        [switch]$QtTranslationsKept
    )

    Get-ChildItem -LiteralPath $BinDir -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -in @('.pdb', '.ilk', '.exp', '.lib', '.ipdb') } |
        ForEach-Object {
            Remove-Item -LiteralPath $_.FullName -Force
        }

    if (-not $QtTranslationsKept) {
        $tr = Join-Path $BinDir 'translations'
        if (Test-Path -LiteralPath $tr) {
            Remove-Item -LiteralPath $tr -Recurse -Force
        }
    }

    foreach ($sub in @(
            'sqldrivers',
            'geoservices',
            'sensors',
            'position',
            'scxmldaemon',
            'assetimporters',
            'texttospeech'
        )) {
        $d = Join-Path $BinDir "plugins\$sub"
        if (Test-Path -LiteralPath $d) {
            Remove-Item -LiteralPath $d -Recurse -Force
        }
    }
}

function Copy-PeDependencyClosure {
    param(
        [Parameter(Mandatory)] [string]$DumpBin,
        [Parameter(Mandatory)] [string]$BinDir,
        [Parameter(Mandatory)] [string[]]$SeedPes
    )
    $pathDirs = @()
    if ($env:PATH) {
        $pathDirs = @($env:PATH.Split([IO.Path]::PathSeparator) | Where-Object { $_ })
    }
    $searchDirs = @($BinDir) + $pathDirs

    $queue = [System.Collections.Queue]::new()
    $scanned = @{}

    foreach ($s in $SeedPes) {
        if ($s -and (Test-Path -LiteralPath $s)) {
            $queue.Enqueue((Resolve-Path -LiteralPath $s).Path)
        }
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
                if (-not $scanned.ContainsKey($existing)) {
                    $queue.Enqueue($existing)
                }
                continue
            }

            $src = Resolve-NonSystemDll -DllName $dllName -SearchDirs $searchDirs
            if (-not $src) { continue }

            Copy-Item -LiteralPath $src -Destination $destPath -Force
            Write-Host "  dep: $dllName <- $src"
            $queue.Enqueue((Resolve-Path -LiteralPath $destPath).Path)
            if (-not ($searchDirs -contains $BinDir)) {
                $searchDirs = @($BinDir) + $searchDirs
            }
        }
    }
}

Push-Location $repoRoot
try {
    if (-not $NoBuild) {
        & $buildScript -Preset release -Clean:$Clean -CraftRoot $CraftRoot -StrictExeIcon:$StrictExeIcon
    }

    if (-not (Test-Path -LiteralPath $exePath)) {
        throw "Executable not found: $exePath (build release first, or drop -NoBuild)"
    }

    Ensure-QtOnPath
    Ensure-CraftRootQtResourcesForWinDeploy -CraftRoot $CraftRoot

    Write-Host "Copying cmark DLLs into $binDir ..."
    Copy-CmarkDllsToBin -BuildRoot $buildDirPath -DestBin $binDir

    $windeployqt = Get-WinDeployQtExe
    Write-Host "Running windeployqt: $windeployqt"
    $wdArgs = @(
        '--release',
        '--compiler-runtime',
        '--skip-plugin-types', 'qmltooling',
        "--verbose", "$WinDeployVerbose"
    )
    if (-not $IncludeQtTranslations) {
        $wdArgs += '--no-translations'
    }
    $wdArgs += $exePath
    & $windeployqt @wdArgs
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt failed (exit $LASTEXITCODE)"
    }

    $dumpBin = Get-DumpBinExe
    if (-not $dumpBin) {
        Write-Warning "dumpbin.exe not found; skipping non-Qt dependency pass (KF6 DLLs may still be missing on other PCs). Install VS C++ tools or run from a VS dev shell."
    }
    else {
        Write-Host "Resolving PE dependencies with: $dumpBin"
        $seeds = @($exePath)
        $webEngine = Join-Path $binDir 'QtWebEngineProcess.exe'
        if (Test-Path -LiteralPath $webEngine) {
            $seeds += $webEngine
        }
        Copy-PeDependencyClosure -DumpBin $dumpBin -BinDir $binDir -SeedPes $seeds
    }

    Write-Host "Pruning deploy folder (symbols, unused plugins; use -IncludeQtTranslations for Qt .qm files) ..."
    Invoke-ReleaseFolderCleanup -BinDir $binDir -QtTranslationsKept:$IncludeQtTranslations

    Write-Host "Release folder ready: $binDir"
    Write-Host "You can run ghostwriter.exe from Explorer or any shell without Craft on PATH."
}
finally {
    Pop-Location
}
