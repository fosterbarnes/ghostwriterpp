# Release inputs (repo-relative; version from Version, e.g. ghostwriter++_v2.1.6-2.3_win64.zip):
#   - .installer/Output/ghostwriterpp-x64-installer.exe
#   - build-release/ghostwriter++_v{version}_win64.zip

$ErrorActionPreference = "Stop"
$Host.UI.RawUI.WindowTitle = "Draft ghostwriterpp Release"

. "$PSScriptRoot\scriptHelper.ps1"

$root = Get-CMakeProjectRoot -ScriptsDirectory $PSScriptRoot
Set-Location -LiteralPath $root

$buildDir = Join-Path $root (Get-BuildDirectoryNameForPreset -Preset release)
$portableZipBuilt = Join-Path $buildDir "ghostwriter++_v${versionContents}_win64.zip"
$installerBuilt = Join-Path $root ".installer\Output\ghostwriterpp-x64-installer.exe"

if (-not (Test-Path -LiteralPath $portableZipBuilt)) {
    Write-Host "Missing portable zip (run .prePush.ps1 first): $portableZipBuilt" -ForegroundColor Red
    exit 1
}
if (-not (Test-Path -LiteralPath $installerBuilt)) {
    Write-Host "Missing installer (run .prePush.ps1 or buildInstaller.ps1): $installerBuilt" -ForegroundColor Red
    exit 1
}

$null = Get-Command git -ErrorAction Stop
$null = Get-Command gh -ErrorAction Stop

Write-Host "Portable zip: $portableZipBuilt"
Write-Host "Installer:    $installerBuilt"
Write-Host "Version:      $versionContents"
if (Test-Path -LiteralPath $buildNotes) {
    Write-Host "Release notes source: $buildNotes" -ForegroundColor Cyan
}

$v = $versionContents
$tagName = "v$v"
$defaultReleaseName = "ghostwriter++ v$v"
$releaseName = $defaultReleaseName
$releaseNotes = ""

$useBuildNotesTxt = $false
if (Test-Path -LiteralPath $buildNotes) {
    if (-not [string]::IsNullOrWhiteSpace($buildNotesContents)) { $useBuildNotesTxt = $true }
}

if ($useBuildNotesTxt) {
    $bnLines = @(Get-Content -LiteralPath $buildNotes -Encoding UTF8)
    $releaseName = $bnLines[0].Trim()
    if ($bnLines.Count -le 1) { $releaseNotes = "" }
    else { $releaseNotes = ($bnLines[1..($bnLines.Count - 1)] -join "`n") }
    Write-Host "`nUsing buildNotes.txt: first line = release title; remaining lines = notes (prompt skipped)." -ForegroundColor Cyan
}
else {
    Write-Host "`nEnter release notes:" -ForegroundColor Yellow
    Write-Host "Tabs will be converted to spaces for GitHub formatting." -ForegroundColor Cyan
    $releaseNotesLines = @()
    $consecutiveEmptyLines = 0
    $hasReleaseNotes = $false

    while ($true) {
        $line = Read-Host ">"
        if ($line -eq "") {
            $consecutiveEmptyLines++
            if ($consecutiveEmptyLines -ge 2) { break }
            $releaseNotesLines += ""
        }
        else {
            $line = $line -replace "`t", "    "
            $releaseNotesLines += $line
            $consecutiveEmptyLines = 0
            $hasReleaseNotes = $true
        }
    }

    if (-not $hasReleaseNotes) {
        Write-Host "Error: No release notes entered." -ForegroundColor Red
        exit 1
    }

    $releaseNotes = $releaseNotesLines -join "`n"
}

$finalPortable = Join-Path $env:TEMP "ghostwriter++Portable_${tagName}_win64.zip"
$finalX64 = Join-Path $env:TEMP "ghostwriter++Installer_${tagName}_win64.exe"

foreach ($p in @($finalPortable, $finalX64)) {
    if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force -ErrorAction SilentlyContinue }
}

Copy-Item -LiteralPath $portableZipBuilt -Destination $finalPortable -Force
Copy-Item -LiteralPath $installerBuilt -Destination $finalX64 -Force

if (git tag -l $tagName) {
    Write-Host "Local tag $tagName exists. Deleting..."
    git tag -d $tagName
}

$remoteTags = git ls-remote --tags origin | ForEach-Object { ($_ -split "`t")[1] }
if ($remoteTags -contains "refs/tags/$tagName") {
    Write-Host "Remote tag $tagName exists. Deleting..."
    git push origin --delete $tagName
}

git tag $tagName
git push origin $tagName

$originUrl = (git config --get remote.origin.url 2>$null).Trim().TrimEnd('/')
if ([string]::IsNullOrWhiteSpace($originUrl)) { throw "git remote origin.url is not set" }
if (-not ($originUrl -match 'github\.com[:/](?<owner>[^/]+)/(?<repo>[^/.]+)(?:\.git)?$')) {
    throw "Could not parse owner/repo from origin for gh (expected github.com HTTPS or SSH URL): $originUrl"
}
$ghRepo = '{0}/{1}' -f $Matches['owner'], $Matches['repo']
& gh release create $tagName "$finalPortable" "$finalX64" --repo $ghRepo --title "$releaseName" --notes "$releaseNotes" --prerelease

Remove-Item -LiteralPath $finalPortable, $finalX64 -Force -ErrorAction SilentlyContinue
