#requires -Version 7.0
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\scriptHelper.ps1"
Set-Location -LiteralPath $repoRoot

function Show-Help {
    Write-Host @"
newVersion.ps1              Bump 5th number (same as -+++++)
newVersion.ps1 -+ / +       Bump 1st number
newVersion.ps1 -++ / ++     Bump 2nd number
newVersion.ps1 -+++ / +++   Bump 3rd number
newVersion.ps1 -++++ / ++++ Bump 4th number
newVersion.ps1 -+++++ / +++++ Bump 5th number
newVersion.ps1 -            Bump 1st number down
newVersion.ps1 --           Bump 2nd number down
newVersion.ps1 ---          Bump 3rd number down
newVersion.ps1 ----         Bump 4th number down
newVersion.ps1 -----        Bump 5th number down
newVersion.ps1 -tag         Set the optional release tag
"@
}

function Update-BuildNotesHeader {
    $ver = (readVerFile)[0]
    $versionLine = "v$ver"

    if (-not (Test-Path -LiteralPath $buildNotes)) {
        writeFileNoBom -LiteralPath $buildNotes -Content ($versionLine + "`n")
        return
    }

    $prev = @([IO.File]::ReadAllLines($buildNotes))
    $existingFirst = if ($prev.Length -gt 0) { $prev[0].Trim() } else { '' }
    if ($existingFirst -notmatch '^v[\d]') {
        Write-Host "buildNotes.txt line 1 kept as custom title: $existingFirst"
        return
    }

    $existingTail = if ($prev.Length -gt 1) { $prev[1..($prev.Length - 1)] } else { @() }
    $content = ((@($versionLine) + @($existingTail)) -join "`n") + "`n"
    writeFileNoBom -LiteralPath $buildNotes -Content $content
}

function Parse-VersionParts {
    $raw = (readVerFile)[0].Trim()
    $parts = [System.Collections.Generic.List[int]]::new()
    $seps = [System.Collections.Generic.List[string]]::new()
    $buf = ''
    foreach ($ch in $raw.ToCharArray()) {
        if ($ch -eq '.' -or $ch -eq '-') {
            if ($buf.Length -eq 0 -or $buf -notmatch '^\d+$') { throw "Invalid version: $raw" }
            $parts.Add([int]$buf)
            $seps.Add([string]$ch)
            $buf = ''
        }
        else { $buf += $ch }
    }
    if ($buf.Length -eq 0 -or $buf -notmatch '^\d+$') { throw "Invalid version: $raw" }
    $parts.Add([int]$buf)
    if ($parts.Count -lt 1) { throw "Invalid version: $raw" }
    return @{ Parts = $parts; Seps = $seps; Raw = $raw }
}

function Format-VersionParts($parts, $seps) {
    $sb = [System.Text.StringBuilder]::new()
    [void]$sb.Append($parts[0])
    for ($i = 0; $i -lt $seps.Count; $i++) {
        [void]$sb.Append($seps[$i])
        [void]$sb.Append($parts[$i + 1])
    }
    return $sb.ToString()
}

$wantTag = $false
$bumpIdx = $null
$dir = 'up'
foreach ($flag in @($args | ForEach-Object { "$_".Trim() } | Where-Object Length)) {
    switch -Regex ($flag) {
        '(?i)^(-h|--help)$' { Show-Help; return }
        '(?i)^(-tag|--tag)$' { $wantTag = $true }
        '^\+{1,5}$' {
            if ($null -ne $bumpIdx) { throw 'Use only one bump flag.' }
            $bumpIdx = $flag.Length - 1
            $dir = 'up'
        }
        '^\-\+{1,5}$' {
            if ($null -ne $bumpIdx) { throw 'Use only one bump flag.' }
            $bumpIdx = $flag.TrimStart('-').Length - 1
            $dir = 'up'
        }
        '^\-{1,5}$' {
            if ($null -ne $bumpIdx) { throw 'Use only one bump flag.' }
            $bumpIdx = $flag.Length - 1
            $dir = 'down'
        }
        default { throw "Unknown argument: $flag" }
    }
}
if ($null -eq $bumpIdx -and -not $wantTag) {
    $bumpIdx = 4
    $dir = 'up'
}

$lines = readVerFile
$tagValue = $lines[1]
if ($wantTag) { $tagValue = Read-Host 'Release tag (leave empty to use v<version>)' }

if ($null -ne $bumpIdx) {
    $parsed = Parse-VersionParts
    $parts = [int[]]@($parsed.Parts)
    $seps = [string[]]@($parsed.Seps)

    if ($bumpIdx -ge $parts.Length) {
        throw "Version $($parsed.Raw) has $($parts.Length) number(s); need at least $($bumpIdx + 1) for that flag."
    }

    if ($dir -eq 'down') {
        if ($parts[$bumpIdx] -gt 0) { $parts[$bumpIdx]-- }
    }
    else {
        $parts[$bumpIdx]++
    }

    $newVer = Format-VersionParts $parts $seps
    writeVerFile -SemVer $newVer -Tag $tagValue -Build $lines[2]
    Write-Host "Version -> $newVer"
    Update-BuildNotesHeader
}
else {
    writeVerFile -SemVer $lines[0] -Tag $tagValue -Build $lines[2]
    Write-Host "Version -> $($lines[0]) (tag updated)"
}

closeOut 3
