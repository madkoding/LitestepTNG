# ==============================================================================
#  switch-theme.ps1  -  Selects the active LiteStep theme
#
#     .\tools\switch-theme.ps1                        # lists the themes
#     .\tools\switch-theme.ps1 "LiteStep Enhanced"    # selects it and restarts
#     .\tools\switch-theme.ps1 Glass -NoRestart
#
#  A theme is a folder themes\<Name>\ with a theme.rc. step.rc is only a loader:
#  it includes theme-select.rc (ThemeName "<Name>") and then that theme's theme.rc.
#  Selecting a theme therefore means rewriting theme-select.rc, nothing else.
# ==============================================================================
param(
    [string]$Theme,
    [string]$LiteStepDir = 'C:\LiteStepRun',
    [switch]$NoRestart
)

$ErrorActionPreference = 'Stop'
$themesDir = Join-Path $LiteStepDir 'themes'
$select    = Join-Path $LiteStepDir 'theme-select.rc'

$themes = Get-ChildItem $themesDir -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName 'theme.rc') } |
    Select-Object -ExpandProperty Name

if (-not $Theme) {
    $current = if (Test-Path $select) { ((Get-Content $select -Raw) -replace '(?s).*ThemeName\s+"([^"]*)".*', '$1').Trim() }
    Write-Host "Themes in ${themesDir}:"
    foreach ($name in $themes) { '  {0} {1}' -f $(if ($name -eq $current) { '*' } else { ' ' }), $name }
    return
}

if ($themes -notcontains $Theme) { throw "No theme '$Theme' in $themesDir (available: $($themes -join ', '))" }

[IO.File]::WriteAllText($select, "ThemeName `"$Theme`"`r`n", (New-Object Text.UTF8Encoding($false)))
Write-Host "Theme selected: $Theme"

if (-not $NoRestart) {
    Stop-Process -Name litestep -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
    Start-Process (Join-Path $LiteStepDir 'litestep.exe') -ArgumentList '-overlay'
    Write-Host 'LiteStep restarted.'
}
