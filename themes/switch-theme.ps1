# ==============================================================================
#  switch-theme.ps1  -  Cambia el tema activo de LiteStep TNG
#
#  Uso:
#     .\themes\switch-theme.ps1                 # lista los temas disponibles
#     .\themes\switch-theme.ps1 Metamorphosis   # aplica ese tema
#     .\themes\switch-theme.ps1 Windows11-Dark -NoReload   # aplica sin recargar
#
#  Cada tema vive en  themes\<Nombre>\step.rc  y "aplicar" = copiarlo a
#  C:\LiteStepTNG\step.rc (con backup timestamp del anterior).
# ==============================================================================
param(
    [string]$Theme,
    [switch]$NoReload
)

$ErrorActionPreference = 'Stop'
$Root      = Split-Path $PSScriptRoot -Parent          # C:\LiteStepTNG
$ThemesDir = $PSScriptRoot                              # C:\LiteStepTNG\themes
$ActiveRc  = Join-Path $Root 'step.rc'
$Exe       = Join-Path $Root 'bin\Release_x64\litestep.exe'

function Get-Themes {
    Get-ChildItem $ThemesDir -Directory |
        Where-Object { Test-Path (Join-Path $_.FullName 'step.rc') } |
        Select-Object -ExpandProperty Name
}

# Sin argumento: listar y salir
if (-not $Theme) {
    Write-Host "Temas disponibles:" -ForegroundColor Cyan
    foreach ($t in Get-Themes) {
        $rc   = Join-Path (Join-Path $ThemesDir $t) 'step.rc'
        $desc = ((Get-Content $rc -TotalCount 2) | Select-Object -Last 1).TrimStart('; ')
        '  {0,-18} {1}' -f $t, $desc
    }
    Write-Host "`nUso: .\themes\switch-theme.ps1 <Nombre>" -ForegroundColor DarkGray
    return
}

# Validar que el tema existe
$themeRc = Join-Path (Join-Path $ThemesDir $Theme) 'step.rc'
if (-not (Test-Path $themeRc)) {
    Write-Error "Tema '$Theme' no encontrado. Disponibles: $((Get-Themes) -join ', ')"
    return
}

# Backup del step.rc activo (timestamp) y aplicar
if (Test-Path $ActiveRc) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    Copy-Item $ActiveRc (Join-Path $Root "step.rc.bak-$stamp") -Force
}
Copy-Item $themeRc $ActiveRc -Force
Write-Host "Tema aplicado: $Theme" -ForegroundColor Green

if ($NoReload) {
    Write-Host "Recarga omitida (-NoReload). Lanza LiteStep o haz !Recycle para verlo." -ForegroundColor DarkGray
    return
}

# --------------------------------------------------------------------------
#  TODO(human): recarga de LiteStep tras aplicar el tema.
#
#  El objetivo: que el cambio se vea de inmediato. Hay dos enfoques y un
#  trade-off real que debes decidir:
#
#   A) Reinicio duro:  Stop-Process litestep -Force  +  Start-Process $Exe
#        - Pros: siempre toma la config nueva, estado 100% limpio.
#        - Cons: parpadeo; se pierde el estado de ventanas/overlay del shell.
#
#   B) Recarga suave:  invocar el bang !Recycle del LiteStep ya corriendo
#        (p.ej. lanzando  $Exe  con un argumento que dispare recycle, o
#         enviando el mensaje; si no corre, arrancarlo).
#        - Pros: sin parpadeo, conserva el shell vivo.
#        - Cons: !Recycle relee step.rc pero no siempre recrea TODO limpio.
#
#  Implementa aqui tu enfoque preferido. Variables utiles: $Exe, $ActiveRc.
#  Debe: si litestep corre -> aplicar la recarga elegida; si no -> arrancarlo
#  en overlay:  Start-Process $Exe -ArgumentList '-overlay', "`"$ActiveRc`""
# --------------------------------------------------------------------------
