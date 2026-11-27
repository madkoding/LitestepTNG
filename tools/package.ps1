# ==============================================================================
#  package.ps1  -  Builds the distributable: a portable zip and the installer
#
#     .\tools\package.ps1 -Version 0.26.0
#     .\tools\package.ps1 -Version 0.26.0 -NoInstaller     # zip only
#
#  Takes an existing build (bin\Release_x64; build the solution first) and the
#  themes, stages them the way an install looks, and produces in dist\:
#     LiteStepTNG-<version>-x64.zip          the staged folder, to unpack anywhere
#     LiteStepTNG-<version>-x64-setup.exe    the installer (needs Inno Setup 6)
#
#  This file is the single list of what ships (themes: those whose theme.json has
#  mode overlay); the installer script only packs the staged folder. CI
#  (.github\workflows\build.yml) runs exactly this.
# ==============================================================================
param(
    [Parameter(Mandatory)][string]$Version,
    [string]$Configuration = 'Release_x64',
    [string]$RepoUrl = '',
    [switch]$NoInstaller
)

$ErrorActionPreference = 'Stop'
$Root  = Split-Path $PSScriptRoot -Parent
$Bin   = Join-Path $Root "bin\$Configuration"
$Dist  = Join-Path $Root 'dist'
$Stage = Join-Path $Dist 'stage'

if (-not (Test-Path (Join-Path $Bin 'litestep.exe'))) { throw "No build in $Bin. Build litestep.sln first." }

# The themes that ship: every folder under themes\ whose theme.json has mode "overlay"
# (the default when the manifest has no mode). A "shell" theme cannot run on top of
# Explorer and stays out; a folder without theme.json is skipped with a warning (a
# theme in the repo should have one); a theme.json that is not valid JSON stops the
# build. Sorted by name (ordinal) so every build stages the same list.
$Themes = @()
$themeDirs = @(Get-ChildItem (Join-Path $Root 'themes') -Directory |
    Where-Object { $_.Name -notin 'original-2002', 'drafts' -and -not $_.Name.StartsWith('.') })
[string[]]$names = @($themeDirs | ForEach-Object { $_.Name })
[Array]::Sort($names, [StringComparer]::Ordinal)
foreach ($name in $names) {
    $manifest = Join-Path $Root "themes\$name\theme.json"
    if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
        Write-Warning "Theme '$name' has no theme.json; not packaged."
        continue
    }
    try { $json = [IO.File]::ReadAllText($manifest) | ConvertFrom-Json -ErrorAction Stop }
    catch { throw "themes\$name\theme.json is not valid JSON: $($_.Exception.Message)" }
    if ($null -eq $json) { throw "themes\$name\theme.json is empty" }
    $mode = if ($json.PSObject.Properties['mode'] -and $json.mode) { [string]$json.mode } else { 'overlay' }
    if ($mode -eq 'overlay') { $Themes += $name }
    elseif ($mode -eq 'shell') { Write-Host "Theme '$name' has mode 'shell'; not packaged." }
    else { Write-Warning "Theme '$name' has an unknown mode '$mode'; not packaged." }
}
if (-not $Themes.Count) { throw 'No theme to package (no theme.json with mode overlay under themes\).' }

if (Test-Path $Dist) { Remove-Item $Dist -Recurse -Force }
New-Item -ItemType Directory -Force $Stage | Out-Null

# --- binaries ---------------------------------------------------------------
$binaries = Get-ChildItem $Bin -File |
    Where-Object { $_.Extension -in '.dll', '.exe' }
$binaries | Copy-Item -Destination $Stage
if (-not ($binaries.Name -contains 'lsapi.dll' -and $binaries.Name -contains 'Core.dll')) {
    throw 'The build is incomplete: lsapi.dll or Core.dll is missing.'
}

# --- configuration ----------------------------------------------------------
foreach ($file in 'step.rc', 'theme-select.rc', 'personal.rc.example', 'theme.lsx', 'theme.lsxstyle', 'README.md', 'LICENSE') {
    $source = Join-Path $Root $file
    if (Test-Path $source) { Copy-Item $source $Stage }
}

# --- shared theme configuration (menus, hotkeys, players) and texts ----------
foreach ($shared in 'config', 'lang') {
    $null = robocopy (Join-Path $Root $shared) (Join-Path $Stage $shared) /E /NFL /NDL /NJH /NJS /NP
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed for $shared (code $LASTEXITCODE)" }
}

# --- themes (reference material and drafts stay in the repo) -------------------
foreach ($theme in $Themes) {
    $source = Join-Path $Root "themes\$theme"
    if (-not (Test-Path (Join-Path $source 'theme.rc'))) { throw "Theme without theme.rc: $theme" }
    $null = robocopy $source (Join-Path $Stage "themes\$theme") /E /XD original-2002 drafts /NFL /NDL /NJH /NJS /NP
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed for theme $theme (code $LASTEXITCODE)" }
}
$global:LASTEXITCODE = 0

# The theme the loader selects must be one that ships.
$selected = ((Get-Content (Join-Path $Stage 'theme-select.rc') -Raw) -replace '(?s).*ThemeName\s+"([^"]*)".*', '$1').Trim()
if ($Themes -notcontains $selected) { throw "theme-select.rc selects '$selected', which is not packaged." }

$count = (Get-ChildItem $Stage -Recurse -File).Count
Write-Host "Staged $count files ($($binaries.Count) binaries, themes: $($Themes -join ', '))"

# --- portable zip -------------------------------------------------------------
$zip = Join-Path $Dist "LiteStepTNG-$Version-x64.zip"
Compress-Archive -Path (Join-Path $Stage '*') -DestinationPath $zip
Write-Host "Zip:       $zip"

# --- installer ----------------------------------------------------------------
if (-not $NoInstaller) {
    $iscc = @(
        (Get-Command iscc -ErrorAction SilentlyContinue).Source,
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    ) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if (-not $iscc) { throw 'Inno Setup 6 (ISCC.exe) not found. Install it, or pass -NoInstaller.' }

    # The installer's own version fields only take numbers: 1.2.3-rc1 -> 1.2.3
    $numeric = if ($Version -match '^(\d+(\.\d+){0,3})') { $Matches[1] } else { '0.0.0' }
    & $iscc /Qp "/DAppVersion=$Version" "/DAppNumericVersion=$numeric" "/DRepoUrl=$RepoUrl" `
        "/DStageDir=$Stage" "/DOutputDir=$Dist" (Join-Path $Root 'installer\LiteStepTNG.iss')
    if ($LASTEXITCODE -ne 0) { throw "ISCC failed (code $LASTEXITCODE)" }
    Write-Host "Installer: $(Join-Path $Dist "LiteStepTNG-$Version-x64-setup.exe")"
}
