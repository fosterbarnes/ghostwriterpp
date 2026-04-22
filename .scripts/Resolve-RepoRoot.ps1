# SPDX-License-Identifier: GPL-3.0-or-later
# Resolves the CMake project root when .ps1 files are invoked from a different working directory or clone.

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
(The script file lives under: $scriptRepoRoot — that tree will not be built unless you cd there or pass -RepoRoot.)
"@
        return $cwd
    }

    return $scriptRepoRoot
}
