# Version helper. Parse $args so -+, -++, -+++, … work unquoted in pwsh.
. "$PSScriptRoot\scriptHelper.ps1"

function Show-NewVersionHelp {
    Write-Host @"
  .\newVersion.ps1 -h               Show help.
  .\newVersion.ps1 -+               Bump 1st number (2.1.6-2.3 -> 3.1.6-2.3)
  .\newVersion.ps1 -++              Bump 2nd number (2.1.6-2.3 -> 2.2.6-2.3)
  .\newVersion.ps1 -+++             Bump 3rd number (2.1.6-2.3 -> 2.1.7-2.3)
  .\newVersion.ps1 -++++            Bump 4th number (2.1.6-2.3 -> 2.1.6-3.3)
  .\newVersion.ps1 -+++++           Bump 5th number (2.1.6-2.3 -> 2.1.6-2.4)
"@
}

function Update-BuildNotesHeader {
    $ver = Read-VersionFile
    $versionLine = "v$ver"

    if (-not (Test-Path -LiteralPath $buildNotes)) {
        Write-RepoUtf8NoBomFile -LiteralPath $buildNotes -Content ($versionLine + [Environment]::NewLine)
        return
    }

    $prev = [System.IO.File]::ReadAllLines($buildNotes)
    $existingFirst = if ($prev.Length -gt 0) { $prev[0].Trim() } else { "" }
    # Only rewrite line 1 when it is already a version title (v…). Keep custom release titles.
    if ($existingFirst -notmatch '^v[\d]') {
        Write-Host "buildNotes.txt line 1 kept as custom title: $existingFirst"
        return
    }

    $existingTail = if ($prev.Length -gt 1) { $prev[1..($prev.Length - 1)] } else { @() }
    $out = ((@($versionLine) + @($existingTail)) -join [Environment]::NewLine) + [Environment]::NewLine
    Write-RepoUtf8NoBomFile -LiteralPath $buildNotes -Content $out
}

function Parse-VersionParts {
    $raw = (Read-VersionFile).Trim()
    $parts = [System.Collections.Generic.List[int]]::new()
    $seps = [System.Collections.Generic.List[string]]::new()
    $buf = ""
    foreach ($ch in $raw.ToCharArray()) {
        if ($ch -eq '.' -or $ch -eq '-') {
            if ($buf.Length -eq 0 -or $buf -notmatch '^\d+$') { throw "Invalid version: $raw" }
            $parts.Add([int]$buf)
            $seps.Add([string]$ch)
            $buf = ""
        }
        else {
            $buf += $ch
        }
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

$flags = @(
    $args |
        ForEach-Object { "$_".Trim() } |
        Where-Object { $_.Length -gt 0 }
)

if ($flags.Count -eq 0) { Show-NewVersionHelp; exit 0 }

$wantHelp = $false
$bumpIdx = $null
foreach ($f in $flags) {
    if ($f -match '^(?i)(-h|--h|-help|--help)$') {
        $wantHelp = $true
        continue
    }
    if ($f -match '^-\++$') {
        $plusCount = $f.Length - 1
        $idx = $plusCount - 1
        if ($null -ne $bumpIdx -and $bumpIdx -ne $idx) {
            throw "Use only one bump at a time (got -$('+' * ($bumpIdx + 1)) and $f)."
        }
        $bumpIdx = $idx
        continue
    }
    throw "Unknown argument: $f`nRun .\newVersion.ps1 -h for usage."
}

if ($wantHelp) {
    if ($null -ne $bumpIdx) { throw "Help cannot be combined with other modes." }
    Show-NewVersionHelp; exit 0
}

if ($null -eq $bumpIdx) { Show-NewVersionHelp; exit 0 }

$parsed = Parse-VersionParts
$parts = [int[]]@($parsed.Parts)
$seps = [string[]]@($parsed.Seps)

if ($bumpIdx -ge $parts.Length) {
    throw "Version $($parsed.Raw) has $($parts.Length) number(s); -$('+' * ($bumpIdx + 1)) needs at least $($bumpIdx + 1)."
}

$parts[$bumpIdx]++

$newVer = Format-VersionParts $parts $seps
Write-Host "Version -> $newVer"
Write-VersionFile -SemVer $newVer
Update-BuildNotesHeader
