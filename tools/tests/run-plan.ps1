# Runs tools\theme-plan.ps1 on one case of tools\tests\fixtures\plan\ (used by
# test_theme_plan.py). Builds a scratch run folder holding a copy of the repo's
# config\module-map.rc and an empty file for each DLL named in the case's present.txt,
# then writes the case's plan as JSON (UTF-8 without BOM) to OutPath.
param(
    [Parameter(Mandatory = $true)][string]$ThemeDir,
    [Parameter(Mandatory = $true)][string]$OutPath
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
. (Join-Path $repo 'tools\theme-plan.ps1')

$run = Join-Path ([IO.Path]::GetTempPath()) ('ls-plan-' + [guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path (Join-Path $run 'config') -Force | Out-Null
    Copy-Item (Join-Path $repo 'config\module-map.rc') (Join-Path $run 'config\module-map.rc')
    $present = Join-Path $ThemeDir 'present.txt'
    if (Test-Path -LiteralPath $present) {
        foreach ($line in [IO.File]::ReadAllLines($present)) {
            $name = $line.Trim()
            if ($name) { [IO.File]::WriteAllText((Join-Path $run $name), '') }
        }
    }
    $plan = Get-ThemePlan -ThemeDir $ThemeDir -LiteStepDir $run
    [IO.File]::WriteAllText($OutPath, (ConvertTo-ThemePlanJson $plan), (New-Object Text.UTF8Encoding $false))
}
finally {
    Remove-Item -LiteralPath $run -Recurse -Force -ErrorAction SilentlyContinue
}
