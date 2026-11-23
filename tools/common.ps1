# Shared by the scripts in this folder (dot-sourced).

# Finds the folder LiteStep runs from. The repo does not know where that is: it is
# given explicitly, or through the LITESTEP_DIR environment variable, or taken from
# the litestep.exe that is running.
function Resolve-LiteStepDir([string]$LiteStepDir) {
    if (-not $LiteStepDir) { $LiteStepDir = $env:LITESTEP_DIR }
    if (-not $LiteStepDir) {
        $running = Get-Process litestep -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($running -and $running.Path) { $LiteStepDir = Split-Path $running.Path -Parent }
    }
    if (-not $LiteStepDir) {
        throw 'Where does LiteStep run from? Pass -LiteStepDir <folder>, set the LITESTEP_DIR environment variable, or start LiteStep first.'
    }
    if (-not (Test-Path (Join-Path $LiteStepDir 'litestep.exe'))) {
        throw "No litestep.exe in '$LiteStepDir'."
    }
    return (Resolve-Path $LiteStepDir).Path
}
