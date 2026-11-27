# ==============================================================================
#  install-theme.ps1  -  Installs a theme (folder or zip) into the run folder
#
#     .\tools\install-theme.ps1 C:\Downloads\MyTheme           # a folder with a theme.rc
#     .\tools\install-theme.ps1 C:\Downloads\MyTheme.zip
#     .\tools\install-theme.ps1 MyTheme.zip -Force             # replace an installed one
#
#  The theme is copied to <run folder>\themes\<Name>, then its plan is written to
#  <run folder>\plans\<Name>.json and printed (what is ok, mapped to an nModule,
#  missing or blocked; spec docs\superpowers\specs\2026-11-26-theme-install-design.md).
#  It does not select the theme and does not restart LiteStep: switch-theme.ps1 does.
#
#  <Name> is the folder name (step.rc builds the theme's paths from it):
#   - a folder: its own name;
#   - a zip holding a single top-level folder: that folder, unwrapped;
#   - a zip with theme.rc at its root: the "name" of its theme.json, else the zip's
#     file name without .zip.
#  A theme.json "name" that differs from <Name> is only a warning. A theme whose name
#  matches an installed one, ignoring case, is already installed: -Force replaces that
#  folder's content and keeps the installed folder's spelling, so theme-select.rc and
#  the plan file keep matching it.
#  Every zip entry is checked to stay inside the extraction folder before anything is
#  extracted; the zip is extracted under %TEMP% and only a validated theme reaches the
#  run folder. Exit code 0 when installed, 1 with a message when refused.
# ==============================================================================
param(
    [Parameter(Mandatory = $true, Position = 0)][string]$Source,
    [switch]$Force,
    [string]$LiteStepDir
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot 'theme-plan.ps1')

$ReservedNames = 'CON', 'PRN', 'AUX', 'NUL', 'COM1', 'COM2', 'COM3', 'COM4', 'COM5', 'COM6', 'COM7',
    'COM8', 'COM9', 'LPT1', 'LPT2', 'LPT3', 'LPT4', 'LPT5', 'LPT6', 'LPT7', 'LPT8', 'LPT9'

# Refuses a theme name that is not a plain folder name.
function Assert-ThemeName([string]$Name) {
    if (-not $Name -or -not $Name.Trim()) { throw 'The theme has no name.' }
    if ($Name.Contains('..') -or $Name.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) {
        throw "'$Name' is not a valid theme folder name."
    }
    if ($Name -ne $Name.Trim() -or $Name.EndsWith('.') -or $ReservedNames -contains ($Name -split '\.')[0]) {
        throw "'$Name' is not a valid theme folder name."
    }
}

# The "name" of the theme.json in $Dir, or $null (no manifest, not JSON, no name).
function Get-ManifestName([string]$Dir) {
    $path = Join-Path $Dir 'theme.json'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $null }
    try { $manifest = [IO.File]::ReadAllText($path) | ConvertFrom-Json -ErrorAction Stop }
    catch { return $null }
    $name = Get-JsonProperty $manifest 'name'
    if ($name -is [string] -and $name) { return $name }
    return $null
}

# Extracts the zip into $Staging after checking that every entry lands inside it.
function Expand-ThemeZip([string]$Zip, [string]$Staging) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $root = Get-RunFolderPrefix $Staging
    $archive = [IO.Compression.ZipFile]::OpenRead($Zip)
    try {
        foreach ($entry in $archive.Entries) {
            $target = $null
            try { $target = [IO.Path]::GetFullPath([IO.Path]::Combine($root, $entry.FullName)) } catch { }
            if (-not $target -or -not $target.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
                throw "The zip entry '$($entry.FullName)' would be written outside the theme folder; nothing was installed."
            }
        }
    }
    finally { $archive.Dispose() }
    [IO.Compression.ZipFile]::ExtractToDirectory($Zip, $Staging)
}

$staging = $null
$exitCode = 0
try {
    $LiteStepDir = Resolve-LiteStepDir $LiteStepDir
    if (-not (Test-Path -LiteralPath $Source)) { throw "Not found: $Source" }
    $Source = (Resolve-Path -LiteralPath $Source).Path
    $warnings = New-Object System.Collections.Generic.List[string]

    # Find the theme folder and its name.
    if (Test-Path -LiteralPath $Source -PathType Container) {
        $themeSource = $Source.TrimEnd('\')
        $name = Split-Path $themeSource -Leaf
    }
    elseif ($Source.EndsWith('.zip', [StringComparison]::OrdinalIgnoreCase)) {
        $staging = Join-Path ([IO.Path]::GetTempPath()) ('ls-theme-' + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $staging | Out-Null
        Expand-ThemeZip $Source $staging
        if (Test-Path -LiteralPath (Join-Path $staging 'theme.rc') -PathType Leaf) {
            $themeSource = $staging
            $name = Get-ManifestName $staging
            if (-not $name) { $name = [IO.Path]::GetFileNameWithoutExtension($Source) }
        }
        else {
            $top = @(Get-ChildItem -LiteralPath $staging -Force | Where-Object { $_.Name -ne '__MACOSX' })
            if ($top.Count -ne 1 -or -not $top[0].PSIsContainer) {
                throw 'The zip has no theme.rc at its root and is not a single folder holding one.'
            }
            $themeSource = $top[0].FullName
            $name = $top[0].Name
        }
    }
    else { throw "Not a folder or a .zip: $Source" }

    if (-not (Test-Path -LiteralPath (Join-Path $themeSource 'theme.rc') -PathType Leaf)) {
        throw "No theme.rc in $themeSource."
    }
    Assert-ThemeName $name
    $manifestName = Get-ManifestName $themeSource
    if ($manifestName -and $manifestName -cne $name) {
        $warnings.Add("theme.json names the theme '$manifestName'; it is installed as '$name' (the folder name).")
    }

    # Already installed? The comparison ignores case, as Windows does.
    $themesDir = Join-Path $LiteStepDir 'themes'
    if (-not (Test-Path -LiteralPath $themesDir)) { New-Item -ItemType Directory -Path $themesDir | Out-Null }
    $existing = Get-ChildItem -LiteralPath $themesDir -Directory -Force | Where-Object { $_.Name -eq $name } |
        Select-Object -First 1
    if ($existing) {
        if (-not $Force) { throw "The theme '$($existing.Name)' is already installed in $themesDir (use -Force to replace it)." }
        if ($themeSource.TrimEnd('\') -ieq $existing.FullName.TrimEnd('\')) {
            throw "$Source is the installed theme itself."
        }
        $name = $existing.Name
    }
    $target = Join-Path $themesDir $name

    # Copy next to the target first and swap folders by renaming, so a failed copy (or a
    # locked file in the installed theme) leaves the installed theme as it was.
    $id = [guid]::NewGuid().ToString('N')
    $incoming = Join-Path $themesDir ".installing-$id"
    $outgoing = Join-Path $themesDir ".replaced-$id"
    try {
        Copy-Item -LiteralPath $themeSource -Destination $incoming -Recurse -Force
        if ($existing) { Rename-Item -LiteralPath $target -NewName (Split-Path $outgoing -Leaf) }
        try { Rename-Item -LiteralPath $incoming -NewName $name }
        catch {
            if ($existing) { Rename-Item -LiteralPath $outgoing -NewName $name }
            throw
        }
        if ($existing) {
            try { Remove-Item -LiteralPath $outgoing -Recurse -Force }
            catch { Write-Warning "The replaced copy could not be removed (in use?): $outgoing" }
        }
    }
    finally {
        if (Test-Path -LiteralPath $incoming) { Remove-Item -LiteralPath $incoming -Recurse -Force -ErrorAction SilentlyContinue }
    }
    Write-Host "Theme installed: $target"

    foreach ($w in $warnings) { Write-Warning $w }
    try {
        $plan = Get-ThemePlan -ThemeDir $target -LiteStepDir $LiteStepDir
        $planPath = Write-ThemePlan $plan $LiteStepDir
        Show-ThemePlan $plan
        Write-Host "Plan: $planPath"
    }
    catch { Write-Warning "The theme is installed but its plan could not be written: $($_.Exception.Message)" }
    Write-Host "Not selected; select it with: .\tools\switch-theme.ps1 `"$name`""
}
catch {
    Write-Host "Not installed: $($_.Exception.Message)" -ForegroundColor Red
    $exitCode = 1
}
finally {
    if ($staging -and (Test-Path -LiteralPath $staging)) {
        Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
    }
}
exit $exitCode
