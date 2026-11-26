# ==============================================================================
#  deploy.ps1  -  Copies a build and the themes to the folder LiteStep runs from
#
#  The repo builds into bin\Release_x64 but the shell runs from another folder.
#  Building does not update the running shell; this does. The target is
#  -LiteStepDir, else the LITESTEP_DIR environment variable, else the folder of
#  the litestep.exe that is running.
#
#     .\tools\deploy.ps1                      # binaries + themes, then restart
#     .\tools\deploy.ps1 -Modules Core,VWM    # only those module DLLs
#     .\tools\deploy.ps1 -ThemesOnly          # no binaries
#     .\tools\deploy.ps1 -Themes Glass        # also installs a theme that is not there yet
#     .\tools\deploy.ps1 -NoRestart           # copy what is not locked, do not restart
#
#  LiteStep is stopped first (its DLLs are locked while it runs) and started again
#  with -overlay. theme-select.rc is only written if the target has none, so the
#  theme selected there is kept; personal.rc, games\ and favorites\ are never touched. By default only the themes already installed in the
#  target are updated: some themes in the repo were written for LiteStep as the
#  Windows shell (they load Desk.dll) and must not appear in an overlay install
#  just because they exist here.
# ==============================================================================
param(
    [string]$LiteStepDir,
    [string]$Configuration = 'Release_x64',
    [string[]]$Modules,
    [string[]]$Themes,
    [switch]$ThemesOnly,
    [switch]$NoRestart
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')
$LiteStepDir = Resolve-LiteStepDir $LiteStepDir
$Root = Split-Path $PSScriptRoot -Parent
$Bin  = Join-Path $Root "bin\$Configuration"

$wasRunning = [bool](Get-Process litestep -ErrorAction SilentlyContinue)
if (-not $NoRestart -and $wasRunning) {
    Stop-Process -Name litestep -Force
    Start-Sleep -Seconds 2
}

if (-not $ThemesOnly) {
    if (-not (Test-Path $Bin)) { throw "Build output not found: $Bin (build the solution first)" }
    $files = if ($Modules) { $Modules | ForEach-Object { Join-Path $Bin "$_.dll" } }
             else { (Get-ChildItem $Bin -File | Where-Object { $_.Extension -in '.dll', '.exe' }).FullName }
    foreach ($file in $files) {
        if (-not (Test-Path $file)) { throw "Not built: $file" }
        try { Copy-Item $file $LiteStepDir -Force }
        catch { Write-Warning "Could not copy $(Split-Path $file -Leaf) (in use?): $($_.Exception.Message)" }
    }
    Write-Host "Binaries: $(@($files).Count) file(s) -> $LiteStepDir"
}

# Loader and themes. Reference material and drafts stay in the repo.
Copy-Item (Join-Path $Root 'step.rc') $LiteStepDir -Force
if (-not (Test-Path (Join-Path $LiteStepDir 'theme-select.rc'))) {
    Copy-Item (Join-Path $Root 'theme-select.rc') $LiteStepDir
}
# Shared theme configuration (menus, hotkeys, players) and texts: owned by the repo, mirrored.
foreach ($shared in 'config', 'lang') {
    $null = robocopy (Join-Path $Root $shared) (Join-Path $LiteStepDir $shared) /MIR /NFL /NDL /NJH /NJS /NP
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed for $shared (code $LASTEXITCODE)" }
}
$themes = Join-Path $Root 'themes'
$installed = Get-ChildItem $themes -Directory | Where-Object {
    ($Themes -contains $_.Name) -or (Test-Path (Join-Path $LiteStepDir "themes\$($_.Name)"))
}
foreach ($theme in $installed) {
    $null = robocopy $theme.FullName (Join-Path $LiteStepDir "themes\$($theme.Name)") /E /XD original-2002 drafts /NFL /NDL /NJH /NJS /NP
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed for theme $($theme.Name) (code $LASTEXITCODE)" }
}
$global:LASTEXITCODE = 0
Write-Host "Themes: $($installed.Name -join ', ')"

if (-not $NoRestart) {
    # Always -overlay: without it LiteStep sees Explorer's taskbar and shows a blocking prompt.
    Start-Process (Join-Path $LiteStepDir 'litestep.exe') -ArgumentList '-overlay'
    Write-Host 'LiteStep restarted.'
}
