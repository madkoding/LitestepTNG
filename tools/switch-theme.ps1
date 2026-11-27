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
#
#  Before that, the theme's plan is built, written to plans\<Name>.json and printed
#  (tools\theme-plan.ps1). Missing modules or fonts are reported but do not stop the
#  switch: the shell runs without them. Order of a switch: plan -> (a future
#  downloader) -> theme-select.rc -> restart.
# ==============================================================================
param(
    [string]$Theme,
    [string]$LiteStepDir,
    [switch]$NoRestart
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')
. (Join-Path $PSScriptRoot 'theme-plan.ps1')
$LiteStepDir = Resolve-LiteStepDir $LiteStepDir
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

# The folder's own spelling (the name is matched ignoring case).
$Theme = $themes | Where-Object { $_ -eq $Theme } | Select-Object -First 1

try {
    $plan = Get-ThemePlan -ThemeDir (Join-Path $themesDir $Theme) -LiteStepDir $LiteStepDir
    $null = Write-ThemePlan $plan $LiteStepDir
    Show-ThemePlan $plan
    $missing = @($plan.items | Where-Object { $_.status -eq 'missing' })
    if ($missing.Count) {
        Write-Warning "$($missing.Count) item(s) missing ($(($missing | ForEach-Object { $_.name }) -join ', ')); switching anyway, the theme runs without them."
    }
}
catch { Write-Warning "No plan for '$Theme': $($_.Exception.Message)" }

[IO.File]::WriteAllText($select, "ThemeName `"$Theme`"`r`n", (New-Object Text.UTF8Encoding($false)))
Write-Host "Theme selected: $Theme"

if (-not $NoRestart) {
    Stop-Process -Name litestep -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
    Start-Process (Join-Path $LiteStepDir 'litestep.exe')
    Write-Host 'LiteStep restarted.'
}
