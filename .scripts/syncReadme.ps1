# SPDX-License-Identifier: GPL-3.0-or-later
# README Windows download hrefs → match .scripts/version (same tag + filenames as .draftRelease.ps1 / gh release).

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "resolveRepoRoot.ps1")
. (Join-Path $PSScriptRoot "scriptHelper.ps1")

$root = Get-CMakeProjectRoot -ScriptsDirectory $PSScriptRoot
Set-Location -LiteralPath $root

$readme = Join-Path $root "README.md"
if (-not (Test-Path -LiteralPath $readme)) { throw "README not found: $readme" }

$versionContents = Read-VersionFileFromScriptsRoot -ScriptsDirectory $PSScriptRoot
$tagName = "v$versionContents"

$base = "https://github.com/fosterbarnes/ghostwriterpp/releases/download/$tagName"
$installerHref = "$base/ghostwriter++Installer_${tagName}_win64.exe"
$portableHref = "$base/ghostwriter++Portable_${tagName}_win64.zip"

$Utf8NoBomEncoding = New-Object System.Text.UTF8Encoding $false
function Write-RepoUtf8NoBomFile {
    param(
        [Parameter(Mandatory)][string]$LiteralPath,
        [Parameter(Mandatory)][string]$Content
    )
    [System.IO.File]::WriteAllText($LiteralPath, $Content, $Utf8NoBomEncoding)
}

$text = Get-Content -LiteralPath $readme -Raw -Encoding UTF8

$installerPattern = 'https://github\.com/fosterbarnes/ghostwriterpp/releases/download/[^/]+/ghostwriter\+\+Installer_[^"\s]+_win64\.exe'
$portablePattern = 'https://github\.com/fosterbarnes/ghostwriterpp/releases/download/[^/]+/ghostwriter\+\+Portable_[^"\s]+_win64\.zip'

$nInstaller = ([regex]::Matches($text, $installerPattern)).Count
$nPortable = ([regex]::Matches($text, $portablePattern)).Count

if ($nInstaller -eq 0 -and $nPortable -eq 0) {
    Write-Warning "No matching ghostwriter++ release download URLs found in README.md (patterns unchanged)."
    exit 0
}

$updated = $text
if ($nInstaller -gt 0) {
    $updated = [regex]::Replace($updated, $installerPattern, $installerHref)
    Write-Host "Installer href ($nInstaller): $installerHref" -ForegroundColor Yellow
}
if ($nPortable -gt 0) {
    $updated = [regex]::Replace($updated, $portablePattern, $portableHref)
    Write-Host "Portable href ($nPortable): $portableHref" -ForegroundColor Yellow
}

$updated = $updated.TrimEnd()
Write-RepoUtf8NoBomFile -LiteralPath $readme -Content $updated
