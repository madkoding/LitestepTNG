# ==============================================================================
#  smoke-test.ps1  -  Checks a running LiteStep without touching mouse or keyboard
#
#     .\tools\smoke-test.ps1
#     .\tools\smoke-test.ps1 -IncludeDesktopSwitch   # also switches desktops and back
#
#  The project has no unit tests; this drives the running shell through its bangs
#  and inspects the windows that result. It opens Character Map as a test window
#  and closes it again. The desktop switch is opt-in because it moves every window
#  of the user away for a couple of seconds.
#
#  Exit code: number of failed checks.
# ==============================================================================
param(
    [string]$LiteStepDir,
    [switch]$IncludeDesktopSwitch
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')
$LiteStepDir = Resolve-LiteStepDir $LiteStepDir

Add-Type @'
using System; using System.Text; using System.Runtime.InteropServices; using System.Collections.Generic;
public static class Smoke {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string c, string t);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr GetProp(IntPtr h, string n);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  public struct RECT { public int L, T, R, B; }

  // Visible top-level windows of a class (and optionally a title).
  public static List<RECT> Windows(string cls, string title) {
    var found = new List<RECT>(); IntPtr h = IntPtr.Zero;
    while ((h = FindWindowEx(IntPtr.Zero, h, cls, title)) != IntPtr.Zero) {
      if (!IsWindowVisible(h)) continue; RECT r; GetWindowRect(h, out r); found.Add(r);
    }
    return found;
  }
  public static RECT Rect(IntPtr h) { RECT r; GetWindowRect(h, out r); return r; }
  public static long DeskTag(IntPtr h) { return GetProp(h, "LSVWMDesk").ToInt64(); }

  // True if a frame bar sits over the top edge of this window.
  public static bool HasFrame(IntPtr h) {
    RECT w = Rect(h);
    foreach (RECT b in Windows("LSFramesDrawable", "Frame")) {
      if (Math.Abs(b.T - w.T) <= 40 && b.L <= w.L + 12 && b.R >= w.R - 12) return true;
    }
    return false;
  }

  // Other programs' large windows that are on screen, as "handle:left".
  public static List<string> OnScreen(int ownPid) {
    var found = new List<string>(); IntPtr h = IntPtr.Zero;
    while ((h = FindWindowEx(IntPtr.Zero, h, null, null)) != IntPtr.Zero) {
      if (!IsWindowVisible(h)) continue; RECT r; GetWindowRect(h, out r);
      if (r.R - r.L < 300 || r.L < -15000 || r.L > 15000) continue;
      uint pid; GetWindowThreadProcessId(h, out pid); if (pid == ownPid) continue;
      found.Add(h.ToInt64() + ":" + r.L);
    }
    return found;
  }
}
'@

$script:failed = 0
function Check([string]$name, [bool]$ok, [string]$detail = '') {
    if ($ok) { Write-Host "PASS  $name" -ForegroundColor Green }
    else { $script:failed++; Write-Host "FAIL  $name  $detail" -ForegroundColor Red }
}
function Bang([string]$command) {
    Start-Process (Join-Path $LiteStepDir 'litestep.exe') -ArgumentList $command
    Start-Sleep -Milliseconds 1500
}

# --- the shell and its modules -------------------------------------------------
$litestep = Get-Process litestep -ErrorAction SilentlyContinue | Select-Object -First 1
Check 'litestep is running' ([bool]$litestep)
if (-not $litestep) { exit 1 }

$select = Get-Content (Join-Path $LiteStepDir 'theme-select.rc') -Raw
$themeName = ($select -replace '(?s).*ThemeName\s+"([^"]*)".*', '$1').Trim()
$themeRc = Join-Path $LiteStepDir "themes\$themeName\theme.rc"
Check "selected theme '$themeName' has a theme.rc" (Test-Path $themeRc)

# A theme's theme.rc only lists its area files; LoadModule lines live in one of them.
$themeFiles = Get-ChildItem (Split-Path $themeRc) -Filter *.rc | Select-Object -ExpandProperty FullName
$wanted = Select-String -Path $themeFiles -Pattern '^\s*LoadModule\s+\$LiteStepDir\$(\S+)' | ForEach-Object { $_.Matches[0].Groups[1].Value }
Check "theme '$themeName' loads modules" (@($wanted).Count -gt 0)
$loaded = $litestep.Modules | ForEach-Object { $_.ModuleName }
foreach ($module in $wanted) { Check "module loaded: $module" ($loaded -contains $module) }

# --- notifications ---------------------------------------------------------------
# Only the test box is cleared: a plain !NotifyClear would also dismiss the user's real
# notifications, in Windows too. A real one may arrive meanwhile, so the counts are
# compared loosely and given a few seconds.
function Wait-Until([scriptblock]$condition) {
    foreach ($try in 1..10) { if (& $condition) { return $true }; Start-Sleep -Milliseconds 500 }
    return $false
}
if ($loaded -contains 'Notify.dll') {
    $boxes = [Smoke]::Windows('LSNotifyDrawable', 'NotifyBox').Count
    Bang '!NotifyTest "Smoke test" "Title" "Body"'
    Check '!NotifyTest shows a box' (Wait-Until { [Smoke]::Windows('LSNotifyDrawable', 'NotifyBox').Count -gt $boxes })
    $shown = [Smoke]::Windows('LSNotifyDrawable', 'NotifyBox').Count
    Bang '!NotifyClear test'
    Check '!NotifyClear test removes the test box' (Wait-Until { [Smoke]::Windows('LSNotifyDrawable', 'NotifyBox').Count -lt $shown })
}

# --- window frames and sending a window to another desktop --------------------------
$test = Start-Process charmap.exe -PassThru
Start-Sleep -Seconds 3
$test.Refresh()
$handle = $test.MainWindowHandle
try {
    if ($loaded -contains 'Frames.dll') {
        Check 'a standard window gets a frame' ([Smoke]::HasFrame($handle))
    }
    if ($loaded -contains 'VWM.dll') {
        $before = [Smoke]::Rect($handle)
        $framesBefore = [Smoke]::Windows('LSFramesDrawable', 'Frame').Count
        Bang "!VWMSend 2 $($handle.ToInt64())"
        $after = [Smoke]::Rect($handle)
        Check '!VWMSend parks the window' ($after.L -eq $before.L + 20000) "left $($before.L) -> $($after.L)"
        Check '!VWMSend tags it with its desktop' ([Smoke]::DeskTag($handle) -eq 2)
        if ($loaded -contains 'Frames.dll') {
            # A window that is on no screen is not decorated, and must not leave its
            # frame behind on the desktop it left.
            Check 'a parked window keeps no frame' ([Smoke]::Windows('LSFramesDrawable', 'Frame').Count -eq $framesBefore - 1) "frames: $framesBefore -> $([Smoke]::Windows('LSFramesDrawable', 'Frame').Count)"
        }
    }
}
finally {
    Stop-Process -Id $test.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
}

# --- switching desktops (moves the user's windows: opt-in) ---------------------------
if ($IncludeDesktopSwitch -and ($loaded -contains 'VWM.dll')) {
    $home1 = [Smoke]::OnScreen($litestep.Id)
    $frames1 = [Smoke]::Windows('LSFramesDrawable', 'Frame').Count
    Bang '!VWMDesk 2'
    $away = [Smoke]::OnScreen($litestep.Id)
    Bang '!VWMDesk 1'
    Start-Sleep -Seconds 1
    $back = [Smoke]::OnScreen($litestep.Id)
    Check 'switching desktops parks the windows' ($away.Count -lt $home1.Count) "on screen: $($home1.Count) -> $($away.Count)"
    Check 'switching back restores every window to its place' (-not (Compare-Object $home1 $back)) "before: $($home1.Count), after: $($back.Count)"
    Check 'the window frames come back with their windows' ([Smoke]::Windows('LSFramesDrawable', 'Frame').Count -eq $frames1) "frames: $frames1 -> $([Smoke]::Windows('LSFramesDrawable', 'Frame').Count)"
    Check 'no transition overlay is left behind' ([Smoke]::Windows('LSVWMTransition', $null).Count -eq 0)
}

# --- still standing --------------------------------------------------------------------
Check 'litestep survived the test' ([bool](Get-Process litestep -ErrorAction SilentlyContinue))

Write-Host ''
if ($script:failed) { Write-Host "$($script:failed) check(s) failed." -ForegroundColor Red } else { Write-Host 'All checks passed.' -ForegroundColor Green }
exit $script:failed
