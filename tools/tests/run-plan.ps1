# Runs tools\theme-plan.ps1 on one case of tools\tests\fixtures\plan\ (used by
# test_theme_plan.py). Builds a scratch run folder holding a copy of the repo's
# config\module-map.rc and a file for each DLL named in the case's present.txt,
# then writes the case's plan as JSON (UTF-8 without BOM) to OutPath.
# A present.txt line is "<file>" (an empty file: architecture unknown) or
# "<file> pe32" / "<file> pe64" (the smallest PE header of a 32-bit / 64-bit DLL,
# generated here: MZ, e_lfanew = 0x40, "PE\0\0", Machine, optional header magic).
param(
    [Parameter(Mandatory = $true)][string]$ThemeDir,
    [Parameter(Mandatory = $true)][string]$OutPath
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
. (Join-Path $repo 'tools\theme-plan.ps1')

function New-PeHeader([int]$Machine, [int]$Magic) {
    $bytes = New-Object byte[] 0x80
    $bytes[0] = 0x4D; $bytes[1] = 0x5A                 # MZ
    $bytes[0x3C] = 0x40                                # e_lfanew
    $bytes[0x40] = 0x50; $bytes[0x41] = 0x45           # PE\0\0
    $bytes[0x44] = $Machine -band 0xFF; $bytes[0x45] = $Machine -shr 8
    $bytes[0x58] = $Magic -band 0xFF; $bytes[0x59] = $Magic -shr 8
    return , $bytes
}

$run = Join-Path ([IO.Path]::GetTempPath()) ('ls-plan-' + [guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path (Join-Path $run 'config') -Force | Out-Null
    Copy-Item (Join-Path $repo 'config\module-map.rc') (Join-Path $run 'config\module-map.rc')
    $present = Join-Path $ThemeDir 'present.txt'
    if (Test-Path -LiteralPath $present) {
        foreach ($line in [IO.File]::ReadAllLines($present)) {
            $parts = @($line.Trim() -split '\s+' | Where-Object { $_ })
            if (-not $parts.Count) { continue }
            $path = Join-Path $run $parts[0]
            $kind = if ($parts.Count -gt 1) { $parts[1] } else { '' }
            switch ($kind) {
                'pe32' { [IO.File]::WriteAllBytes($path, (New-PeHeader 0x14c 0x10b)) }
                'pe64' { [IO.File]::WriteAllBytes($path, (New-PeHeader 0x8664 0x20b)) }
                '' { [IO.File]::WriteAllText($path, '') }
                default { throw "present.txt: unknown kind '$kind'" }
            }
        }
    }
    $plan = Get-ThemePlan -ThemeDir $ThemeDir -LiteStepDir $run
    [IO.File]::WriteAllText($OutPath, (ConvertTo-ThemePlanJson $plan), (New-Object Text.UTF8Encoding $false))
}
finally {
    Remove-Item -LiteralPath $run -Recurse -Force -ErrorAction SilentlyContinue
}
