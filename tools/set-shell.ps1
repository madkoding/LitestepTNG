# ==============================================================================
#  set-shell.ps1  -  Chooses what Windows starts as this user's shell at sign-in
#
#     .\tools\set-shell.ps1                 # shows the current choice
#     .\tools\set-shell.ps1 LiteStep        # LiteStep is the shell (from the next sign-in)
#     .\tools\set-shell.ps1 Explorer        # back to Explorer, with LiteStep on top
#
#  Only the current user is changed (HKCU\...\Winlogon "Shell"), no administrator
#  rights are needed, and nothing happens until the next sign-in.
#
#  LiteStep as the shell: Explorer does not start. LiteStep starts ShellAppRuntime.exe
#  (Windows 11), which shows Store apps and Settings and, like Explorer, runs the programs
#  of the Run keys; nWallpaper paints the desktop background. The Run key's own LiteStep
#  entry ("-autostart" or "-overlay") then finds LiteStep running and leaves.
#  Quitting LiteStep (!Quit, or "Quit" in its recovery menu) hands the session to
#  Explorer. If the session ever comes up without a working desktop: Ctrl+Alt+Del ->
#  Task Manager -> Run new task -> powershell, then run this script with Explorer, or:
#     Remove-ItemProperty 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\Winlogon' Shell
# ==============================================================================
param(
    [ValidateSet('LiteStep', 'Explorer', '')]
    [string]$Shell,
    [string]$LiteStepDir
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')
$LiteStepDir = Resolve-LiteStepDir $LiteStepDir
$key = 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\Winlogon'
$exe = Join-Path $LiteStepDir 'litestep.exe'

$current = (Get-ItemProperty $key -Name Shell -ErrorAction SilentlyContinue).Shell
if (-not $Shell) {
    if ($current) { Write-Host "Shell for this user: $current" }
    else { Write-Host 'Shell for this user: not set (Windows default, explorer.exe)' }
    return
}

if ($Shell -eq 'LiteStep') {
    if (-not (Test-Path $exe)) { throw "No litestep.exe in $LiteStepDir." }
    if (-not (Test-Path "$env:WINDIR\System32\ShellAppRuntime.exe")) {
        Write-Warning 'ShellAppRuntime.exe is missing (it ships with Windows 11): Store apps and Settings will not show without Explorer.'
    }
    Set-ItemProperty $key -Name Shell -Value "`"$exe`""
    Write-Host "LiteStep will be the shell from the next sign-in ($exe)."
}
else {
    Remove-ItemProperty $key -Name Shell -ErrorAction SilentlyContinue
    Write-Host 'Explorer will be the shell from the next sign-in (LiteStep starts on top of it from the Run key, if it is there).'
}
