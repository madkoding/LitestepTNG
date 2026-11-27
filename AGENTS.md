# AGENTS.md — LiteStepTNG

Context guide for AI agents (and humans) working on this repo. Read this first so you
don't re-derive what previous sessions already learned.

---

## 1. What this project is

LiteStepTNG is a modern x64 port/evolution of the classic **LiteStep** Windows shell
replacement, written in **C++14/17** with **Direct2D/DirectWrite** rendering. It runs on
top of Explorer, which stays the Windows shell (§3); the old mode that replaced
`explorer.exe` was removed on 2026-11-26.

- **Language/stack:** C++ (MSVC), Direct2D/DirectWrite, Win32, C++/WinRT (for media),
  V8 embedded in the Core module (JS scripting), TagLib (media metadata).
- **Build system:** MSBuild / Visual Studio 2022, `litestep.sln`, configuration
  `Release|x64` (also Debug).
- **UI framework:** the **nModules** suite — a set of module DLLs (`nLabel`, `nTaskbar`,
  `nTray`, `Popup/nPopup`, `StartMenu`, `Clock`, `Core`, etc.) that share a static lib
  **`ModuleKit`** linked into every module.

### Repo layout
```
litestep/          core shell executable (litestep.exe), themev2 engine
lsapi/             LiteStep API headers/defines (lsapi.h, lsapidefines.h)
modules/           the nModules module DLLs (see list below)
  ModuleKit/       shared static lib: Window, WindowSettings, bangs, MonitorInfo...
  Core/            nCore: dynamic text functions ([Time],[CPU],[NowPlaying]...), V8
  CoreCom/         thin client to talk to Core
  TaskBar/         nTaskbar: task buttons
  SysTray/         nTray: notification area
  Popup/           nPopup: popup menus (InfoItem, FolderItem, CommandItem, SeparatorItem)
  StartMenu/       start-menu popup variant (reuses Popup's item classes)
  Label/ Clock/    nLabel / clock labels
  HotKey/ VWM/ Icon/ Panels/ Wallpaper/ MediaInfo/ Calendar/ TaskSwitcher/
  External/        third-party (v8, etc.)
  Notify/          nNotify: Windows notifications as boxes on the desktop
  Frames/          nFrames: skinned frames around other programs' windows (experimental)
  Utilities/       headers shared by modules (WindowActivation.h: forcing the foreground)
utility/           logger etc.
bin/Release_x64/   build output (DLLs + litestep.exe)
step.rc            the loader: includes theme-select.rc, the optional personal.rc, then
                   themes\<ThemeName>\theme.rc
theme-select.rc    ThemeName "<folder in themes\>"
personal.rc.example  template for personal.rc (see "Personal content" in §2)
themes/<Name>/     one folder per theme: theme.rc lists the theme's area files (source of
                   truth; deployed to the run folder by tools\deploy.ps1 — see §2 and §5)
config/            look-free pieces themes include: menus\ (programs, Windows settings,
                   capture, session, media), hotkeys.rc, media-players.rc, module-map.rc
                   (classic module names -> nModules; see §5)
tools/             deploy.ps1, smoke-test.ps1, switch-theme.ps1, install-theme.ps1, theme-plan.ps1
                   (common.ps1 finds the run folder), rc_effective.py (what a theme configures;
                   see §5), tests\ (python -m unittest)
```

---

## 2. Build & deploy

**The repo is built in place, but the shell runs from a separate "run folder".** Building
does not update the running shell — the build has to be deployed there. The repo does not
know where that folder is: the scripts in `tools\` take `-LiteStepDir`, else the
`LITESTEP_DIR` environment variable, else the folder of the litestep.exe that is running.

### Build (PowerShell, from the repo root)
```powershell
# MSBuild.exe of Visual Studio 2022 (or its Build Tools); use its full path if it is not on PATH
msbuild litestep.sln /p:Configuration=Release /p:Platform=x64 /m /verbosity:minimal /nologo
# Output lands in bin\Release_x64\
```
- A change in **`ModuleKit`** (static lib) requires rebuilding **every** module that links
  it — build the whole solution, don't just build one module.
- Check for real errors with: `... 2>&1 | Select-String ': error|fatal error|error C'`.

### Deploy to the runtime
```powershell
.\tools\deploy.ps1                    # every built binary + the installed themes, then restart
.\tools\deploy.ps1 -Modules Core,VWM  # only those DLLs
.\tools\deploy.ps1 -ThemesOnly
```
It stops litestep (its DLLs are locked while it runs), copies, and starts it again.
Only themes already installed in the target are updated (`-Themes <name>` adds one).

**The repo is the source of the themes.** Edit `themes\<Name>\theme.rc` here and deploy;
do not edit the deployed copy in the run folder (older sessions did, and then copied it back).
Deploy also rewrites the plan (see below) of every theme installed in the run folder, since
the modules may have changed; a plan that fails is only a warning.

### Install a theme that is not in the repo (since 2026-11-27)
```powershell
.\tools\install-theme.ps1 <folder | file.zip> [-Force] [-LiteStepDir <run folder>]
.\tools\switch-theme.ps1 "<Name>"     # selects it and restarts; -NoRestart to only select
```
- `install-theme.ps1` checks the theme (a `theme.rc` at the root of the theme folder, no zip
  entry that escapes the extraction folder, the name not installed yet, ignoring case, unless
  `-Force`), copies it to `<run folder>\themes\<Name>`, writes its plan and prints it. It does
  not select the theme and does not restart LiteStep. A zip is extracted under `%TEMP%`
  first; only a checked theme reaches the run folder. Exit code 0 = installed, 1 = refused.
- `<Name>` is the folder name, because `step.rc` builds the theme's paths from it: a folder
  keeps its name; a zip with one top-level folder is unwrapped to it; a zip with `theme.rc` at
  its root takes the `name` of its `theme.json`, else the zip's file name. A manifest `name`
  that differs from the folder is only a warning. `-Force` keeps the installed folder's
  spelling, so `theme-select.rc` and the plan file still match it.
- `switch-theme.ps1` builds, writes and prints the plan before it rewrites
  `theme-select.rc`. Missing modules or fonts are reported but do not stop the switch: the
  shell runs without them.
- The plan is `<run folder>\plans\<Theme>.json`, one per theme (a shared file would keep only
  the last theme deployed). Format, statuses and the downloader's contract are in §5
  "Theme installation".
- Nothing here downloads. A classic theme's `*NetLoadModule` lines used to fetch modules from
  a mirror that no longer exists; now they go through the module map (§5) and what is still
  missing is only reported.

### Personal content stays out of the repo
Nothing that belongs to one person or one computer goes into tracked files: no user names,
no absolute paths of an install, no lists of someone's programs. Such things live in the
run folder, which the deploy script never overwrites:
- `personal.rc` — plain settings that override the theme. `step.rc` includes it before the
  theme and the first definition of a key wins (`SettingsManager::_FindLine`); a missing
  file is skipped silently. `*` lines (menus, hotkeys) add to the theme's, they do not
  replace them, so a personal menu cannot be done this way.
- `games\` — shortcuts shown under JUEGOS (`!PopupFolder:"$LiteStepDir$games"`).
- `favorites\` — shortcuts shown under FAVORITOS, managed from the menu.

In a theme, use `$LiteStepDir$`, `$ThemeName$`, `$USERNAME$` and `%LOCALAPPDATA%`-style
paths instead of literal ones.

### Package and CI
`.\tools\package.ps1 -Version <v>` stages a build plus the themes into `dist\stage`. The themes
are the folders under `themes\` whose `theme.json` has `mode` `overlay` (or no `mode`), in
ordinal name order; a folder without `theme.json` is skipped with a warning, a `theme.json`
that is not valid JSON stops the build, and `original-2002\` / `drafts\` inside a theme are
left out (robocopy `/XD`). Since 2026-11-27 this replaces a hard-coded list; today it
yields the four themes in the repo. The script still fails when `theme-select.rc` selects a
theme that does not ship. It produces `dist\LiteStepTNG-<v>-x64.zip` and, with Inno Setup 6
installed, `dist\LiteStepTNG-<v>-x64-setup.exe` (`installer\LiteStepTNG.iss` only packs the
staged folder — the list of what ships lives in the script). The installer is per user
(no admin), installs to `%LOCALAPPDATA%\Programs\LiteStepTNG`, leaves Explorer as the
shell, optionally adds the `Run` value, never overwrites
`theme-select.rc`, and stops only the LiteStep running from its own folder (`!Quit`
first, so Explorer's taskbar comes back; kill as a last resort).

`.github\workflows\build.yml` (GitHub Actions, `windows-2022`) builds Release x64 and runs
that script on every push to `master` and every pull request, keeping the two files as
an artifact; pushing a tag `v<version>` publishes them as a GitHub release.

**A clean clone must build.** The working tree here hides missing pieces (old binaries
in `bin\` are reused): `TagLib` was not in the solution and nobody noticed until a fresh
clone failed to link `MediaInfo`. To check before touching the build: `git clone` into a
scratch folder and build there.

### Test
There are no unit tests. `.\tools\smoke-test.ps1` drives the running shell through its
bangs and inspects the resulting windows (modules loaded, notifications — only test boxes: `!NotifyClear test` — window frames,
sending a window to another desktop); `-IncludeDesktopSwitch` also switches desktops and
back, which moves the user's windows away for a couple of seconds. Exit code = failed checks.

---

## 3. Runtime architecture (IMPORTANT — changed 2026-11-21 and 2026-11-26)

**Two modes, chosen by who owns the shell window when litestep starts.**
- **On top of Explorer** (the default, and the setup of this machine): Explorer is the shell,
  litestep is a program started from the `Run` key. Everything below is about this mode
  unless it says otherwise.
- **Session shell** (since 2026-11-26, `tools\set-shell.ps1 LiteStep`): litestep is the
  Winlogon shell and Explorer does not run. See "Session shell" below.
```
HKCU\Software\Microsoft\Windows NT\CurrentVersion\Winlogon\Shell   (no value; the HKLM default explorer.exe applies)
HKCU\Software\Microsoft\Windows\CurrentVersion\Run\LiteStep = "<run folder>\litestep.exe"
```
The earlier setup (litestep as the Winlogon shell, explorer not running) was abandoned
because too much of Windows depends on Explorer's immersive shell: UWP apps (Settings,
Calculator), `ms-settings:` links, media-key routing, native virtual desktops. See §8 for
what was tried. On 2026-11-26 the code for it was removed: the start-up prompt, closing
and restarting Explorer, `SetShellWindow`, the Winlogon prompt, `StartupRunner` (the Run
keys and Startup folder, which Explorer runs), `ExplorerService`, the logon sound, the
recovery menu's "Start Explorer as shell", the `LSOverlayMode` / `LSSetAsShell` /
`LSNoShellWarning` / `LSNoStartup` settings, the `Desk.dll` module and the themes that
needed it (`Windows11-Dark` was the same as Glass; `Windows-nModules` was ported).
`-nostartup`, `-startup`, `-explorer` and `-closeexplorer` are still accepted and ignored;
`-overlay` now means `-autostart` (see "Session shell"), so old `Run` values keep working. The tray service stays: it is
what gives nTray the icons. In safe mode litestep exits at once.

### Session shell (ShellAppRuntime)
The old shell mode failed because nothing provided Explorer's *immersive shell*: Store
apps started but their `ApplicationFrameHost` window stayed cloaked (§8). Windows 11 ships
`ShellAppRuntime.exe` (and `CustomShellHost.exe`), the hosts it uses for kiosk shells. On
2026-11-26, with Explorer closed, starting `ShellAppRuntime.exe` made it the owner of the
shell window, and Calculator and Settings opened and showed (activated through
`IApplicationActivationManager`; with no host at all, activation failed with 0x80040900).
How litestep uses it (`litestep.cpp`):
- `GetShellWindow()` is null at start-up => litestep is the session's shell. It starts
  `LSShellHost` (default `%WINDIR%\System32\ShellAppRuntime.exe`, `""` = none) before
  anything else and waits up to 10 s for it to own the shell window. litestep never calls
  `SetShellWindow`: the shell window must be the host's.
- The Run keys are run by ShellAppRuntime itself (about 40 s after sign-in, seen on the
  first real sign-in; it ignores Explorer's `StartupHasBeenRun` marker), so litestep does
  not run them: a `StartupRunner` tried on 2026-11-26 started them a second time.
  ShellAppRuntime also starts the `Run\LiteStep` entry: a litestep started with
  `-autostart` (or the older `-overlay`) exits when another one is running, instead of
  replacing it as a manual start does. The installer writes `-autostart`.
- nWallpaper paints the desktop (`DesktopBackground.cpp`, window class
  `LSDesktopBackground`) only when no window holds a `SHELLDLL_DefView`. ShellAppRuntime
  creates a `Progman` of its own, so "is there a Progman" is the wrong test. The window
  stays at the bottom and runs `DesktopRightClick` / `DesktopMiddleClick`; `OnDesktop`
  windows (ModuleKit) go right above it.
- `!Quit` (or `LR_EXPLORER`) stops the host and starts explorer.exe, which then becomes the
  shell. Sign-out and shut-down do not. Windows then starts the Winlogon shell again
  (`userinit` launched a new litestep one second later, seen 2026-11-27), which finds
  Explorer's shell window and runs on top of it: in this mode `!Quit` turns the session
  into the usual "Explorer + LiteStep on top" rather than closing LiteStep.
- A litestep restarted inside such a session (deploy, crash) is still the session's shell:
  it finds `LSShellHost` owning the shell window (`_FindRunningShellHost`) and keeps a
  handle to it for `!Quit`. Before that fix, a restarted litestep would have quit to a
  session with no UI.
- Tested without signing out (close Explorer gracefully with message 0x5B4 to its
  `Shell_TrayWnd`, so Windows does not restart it; then start litestep): host started,
  background painted, Calculator shown, `!Quit` handed the session back. A real sign-in
  worked ("funciona perfecto como shell", the user, 2026-11-26) apart from the Run entry
  restarting litestep, fixed above. Not checked one by one: `ms-settings:` links, toasts,
  media keys, Explorer folder windows.

Consequences:
- **Restart litestep with:** `Stop-Process -Name litestep -Force`, then
  `Start-Process <run folder>\litestep.exe`. Nothing respawns it.
- `litestep.exe !Bang args` (via `Start-Process ... -ArgumentList '!Bang args'`, so the
  command line starts with `!` unquoted) reaches the running instance in this mode and
  is the way to test bangs without touching the mouse.
- **Explorer's taskbar is hidden by nCore, by default in every theme** (`HideExplorerTaskbar
  false` in a theme keeps it; before 2026-11-23 the default was the other way round and
  Glass and Prototype-X showed Windows' taskbar): a
  1 s timer re-hides `Shell_TrayWnd` / `Shell_SecondaryTrayWnd`, and they are shown again
  when Core unloads. If litestep is killed or crashes the taskbar stays hidden until
  litestep (or Explorer) is restarted.
- **Work area:** `ReserveScreenTop` / `Bottom` / `Left` / `Right` (px, primary monitor;
  all zero = the whole monitor, so the strip Explorer keeps for its hidden taskbar is not
  left empty). Each theme reserves what its own bars need. Core
  writes `SPI_SETWORKAREA` **without** `SPIF_SENDCHANGE` and re-checks every second.
  Broadcasting the change makes Explorer recompute its own value immediately (a fight
  litestep loses). AppBars are not usable: `SHAppBarMessage` is answered by litestep's own
  `Shell_TrayWnd` (tray service) and crashed in `shcore.dll`.
- **Explorer's desktop view must stay Explorer's.** The removed `Desk.dll` took Explorer's
  `SHELLDLL_DefView` (the window that paints the wallpaper and the icons) and re-parented
  it into its own window; killing litestep destroyed it, leaving a flat grey desktop until
  explorer.exe restarted (2026-11-23). Nothing may re-parent that window again. Check with:
  does a top-level window (normally `Progman`) have a `SHELLDLL_DefView` child?
- **Desktop clicks:** the desktop is Explorer's. `DesktopRightClick <bang>` /
  `DesktopMiddleClick <bang>` (nHotKey, low-level mouse hook) run a bang for clicks on
  Explorer's desktop; SHIFT+click passes through to Explorer.
- **Fullscreen detection:** `litestep/FullscreenMonitor.cpp` ignores cloaked windows and
  the shell desktop (`Progman`, `WorkerW`, `GetShellWindow()`). Without that, focusing
  Explorer's desktop counted as a fullscreen app and every module dropped behind it —
  this is what made overlay mode look broken in earlier sessions.
- **Popups** call `SetForegroundWindow` (with an `AttachThreadInput` fallback) when shown,
  but closing on an outside click does not rely on activation (the user reported popups
  staying open with that alone): while a root popup is open, `Popup.cpp` installs a
  `WH_MOUSE_LL` hook and posts `WM_ACTIVATE(WA_INACTIVE, -1)` to the root for any button
  press that lands outside the module's popup windows.

### Verification workflow (no human in the loop)
There is no on-screen console; verify visually with screenshots. Pattern used throughout:
```powershell
# full-screen grab of the primary monitor
Add-Type -AssemblyName System.Windows.Forms,System.Drawing
$pb=[System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp=New-Object System.Drawing.Bitmap $pb.Width,$pb.Height
$g=[System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen(0,0,0,0,$bmp.Size); $g.Dispose()
$bmp.Save("$scratch\full.png")
```
A `crop.ps1` helper in the session scratchpad crops+scales a region for inspection. To
open menus for inspection, synthesize mouse clicks via `user32!SetCursorPos` + `mouse_event`
(the MENU button is the `MMenuHit` label; `!PopupContext` on left-click).

### Logging
Release builds compile out most logging; `litestep.log` is usually absent. For deep
debugging, instrument with a temporary file-logger (printf-style) and **remove the
scaffolding after** — see the taskbar fix in §6 for the method that worked.

---

## 4. The active theme: "LiteStep Enhanced" (port of the classic theme of that name)

Goal of the ongoing work: port Norman Kaczmarek's 2002 "LiteStep Enhanced" theme to this
build, matching the original theme's preview screenshot (1024×768; not part of the repo).

- **The theme is called "LiteStep Enhanced"; do not use any other name for it** (the user
  asked twice). Earlier sessions called the port by the original's internal `ThemeName`;
  the folder was renamed on 2026-11-23, and the work branch that carried the old name is gone
  (the repo has only `master`).
  All that is left of the old name is the `MM` prefix of the theme's variables and
  `original-2002\Theme.rc`, the author's untouched file. `theme.rc` builds its paths from
  `$ThemeName$`, so the folder name is the only place the name lives. In the repo the port
  shares `themes\LiteStep Enhanced\` with the 2002 original's files, whose `Theme.rc` is kept
  under `original-2002\` (its `Theme.rc`, Config, icons and the 69 .bmp images); an earlier,
  image-less port attempt is kept as `drafts\first-port.rc`. Neither is deployed.
- Deployed theme files: **`<run folder>\themes\LiteStep Enhanced\`** (`theme.rc` + area files, `images\`, `sounds\`; see §5 "Theme files").
- Selected via `theme-select.rc` in the run folder → `ThemeName "LiteStep Enhanced"`, included by
  `step.rc`. Other themes (Glass, Prototype-X) remain switchable the same way.
- Loader chain: `step.rc` → `theme-select.rc` (`ThemeName "X"`) → `themes\X\theme.rc`.

### Layout (matches the preview)
- **Top bar:** clock+date (`MClockTime`/`MClockDate`), virtual desktops, **MENU** button
  (`MMenuHit`), username strip (`MUser`, injektion3.png; no text since 2026-11-26), now-playing
  (`MNowPlaying`, green, live), media controls (prev/play/stop/next on `injektion2.png`),
  TRACK/VOLUME. Left panel = `injektion.png`, right panel = `injektion2.png`.
- **Popup menu** (`*Popup` via `!PopupContext`): dark left rail with icons, beveled title,
  **purple uppercase text (`#FF814090`)**, separators, folder submenu ☐ box, selection
  highlight, red power icon, and a live **date/time box** at the bottom.
- **Bottom:** tasks (`MTasks` = `nTaskbar`, full width), **lslnkmenu** category bar
  (OFFICE/GRAPHIC/MULTIMEDIA/INTERNET/TOOLS — image buttons that open `!PopupLnk*`),
  systray (`MTray` = `nTray`, bottom-right on `systray.png`).

### Key theme features implemented
- **Global UPPERCASE option:** `TextUppercase true` in theme.rc → honored in ModuleKit
  (see §6). The theme also uses the authentic font **`hooge 05_55`** (`MMFont`), installed
  on the machine by the user.
- **Live now-playing** from Windows media sessions (GSMTC) — see §6.
- **Live date/time inside the popup** via `!Info "FECHA:  [Time('%d . %m . %Y')]"` etc.
  (popup InfoItems already evaluate dynamic text — `PopupSettings.cpp` sets
  `infoDefaults.evaluateText = true`).
- **Folder submenu box:** `popupfolder.png` / `popupfolderhover.png` = base item images
  with a small square drawn in the fixed right edge; assigned only to folder items with
  `MMMenuFolderItemImageEdgesRight 16` + `MMMenuFolderItemTextOffsetRight 22`.
- **Popup skin = original pieces, do not "notch" them:** the popup *window* brush is
  `popupframe.png` (popuptop + popupnorm + popupbottom stacked, 178×54) in `Edges` mode
  (`ImageEdgesTop/Bottom 18`, `Left 64`, `Right 20`) with `MMMenuPaddingTop/Bottom 18`, so
  every popup (submenus and content popups like `!PopupThemes` too) gets the header and the
  cut-corner footer with no code. Items use the untouched `popupnorm`/`popupselect`
  (originals also kept as `.bak`; a rejected notched variant is in `*.png.notched`),
  separators use `popupseperator.png` at 18px, InfoItems are plain centered `popupnorm` rows.
  Folder items set the `Open` state image too, so the parent stays highlighted.
- **Pixel font rules (`hooge 05_55`):** it is designed for size 8 — use `FontSize 8` (16 for
  the clock), never 10 (renders 6.25px caps, blurry). Every top-level window that shows text
  sets `<Prefix>TextAntiAliasMode Aliased`, otherwise centered text lands on half pixels.
  Text colours sampled from the preview: popup `#FF40084A`, username `#FF500058` (on the
  `injektion3.png` strip, which is the `MUser` background), clock white.
- **Clock drop shadow:** the engine has stroke but no text shadow, so `MClockTimeSh` /
  `MClockDateSh` are black twin overlay labels offset +1,+1, listed *before* the white ones
  in `*MBarLeftOverlayLabel`. `MNowPlaying` is an overlay child of `MBarRight`, placed inside
  the dark box of `injektion2.png` (x 238..387, y 0..16).
- Classic magenta (`FF00FF`) skin regions were converted to alpha transparency across the
  PNG set; `popuptop2.png` keeps transparent notched corners.

---

## 5. nModules concepts you need (theme.rc authoring)

- **Image brushes:** `<Prefix>BrushType Image`, `<Prefix>Image "path"`,
  `<Prefix>ImageScalingMode Stretch|Tile|Edges`. For 9-slice use `Edges` +
  `<Prefix>ImageEdgesLeft/Top/Right/Bottom` (the edge px stay fixed, the center stretches).
  `Hover`/`Active` variants exist (`<Prefix>HoverImage`, ...).
- **Overlay labels:** `*<Parent>OverlayLabel child1 child2` creates child labels. The
  children must be defined contiguously after the parent and must **NOT** have their own
  `*nLabel` line (that double-creates them in `gAllLabels` and breaks z-order/visibility).
- **Dynamic text:** functions like `[Time('%H:%M:%S')]`, `[CPU]`, `[Memory]`,
  `[NowPlaying]` are registered in Core and re-evaluated via `DynamicTextChangeNotification`
  from Core's `WM_TIMER` (every 1000 ms). Only windows whose `evaluateText` is true parse
  them (labels/clock/popup-info default true).
- **Icons on menu items:** `.icon="file,index"` (uses `ExtractIcon`; works for exe/dll/ico).
  Parsed as the first token of a `*Popup`/`*PopupStartMenu`/`*nLabel`-menu line.
- **Encoding:** write `theme.rc` as **UTF-8 without BOM**. Do **not** use
  `Set-Content -Encoding ASCII` (it corrupts accents, e.g. `Configuración`). Use:
  `[System.IO.File]::WriteAllText($f,$t,(New-Object System.Text.UTF8Encoding($false)))`
  — or just the `Edit`/`Write` tools, which are safe.

### Shadows (since 2026-11-27)
- **Window shadows** (menus, notifications, bars, any top-level window):
  `<Prefix>ShadowPreset none|subtle|classic|win11|macos|material|raytraced|panel|dock|pill|tray`
  and per layer `<Prefix>Shadow<N>OffsetX|OffsetY|Blur|Spread|Opacity|Color|Enabled`, in px
  (a layer key changes layer N of the preset). `ModuleKit/ShadowWindow.cpp` draws them in a
  second window (class `LSShadow`: layered, click-through, kept right below its window,
  same topmost band): a window can only paint inside its own rectangle, so the old code,
  which drew scaled copies of the shape inside the window, never showed. The shape is the
  state's rounded rectangle, or, when the background is a picture, the picture's alpha laid
  out at the window's size exactly as `Brush` draws it (`ModuleKit/ShapeMask.cpp`: Stretch,
  Tile, Center, Fit, Fill and Edges, copying `Brush::ComputeEdgeData`'s mapping, where the
  middle of an Edges picture is scaled by the whole width), so LiteStep Enhanced's cut corners
  and diagonal panel edges cast a matching shadow (the user asked for it: a plain rectangle
  under them looked wrong). Spread grows a picture mask with a max filter. The shape is cut
  out of the shadow (as CSS does), so translucent windows do not show their own shadow.
  Check a shape by giving it `Shadow1Color #FFFF0000`, `Opacity 1`, `Blur 2`, `Spread 4` in
  personal.rc: a red outline that must hug the drawing. `OnDesktop` windows skip `LSShadow`
  windows when stacking.
- **Text shadow** (any window): `<Prefix>TextShadowColor` (default none),
  `TextShadowOffsetX|Y` (1), `TextShadowBlur` (px, 2; 0 = crisp copy).
- **Desktop icon picture**: `<Icons>IconImageShadowColor|OffsetX|OffsetY|Blur`.
- Text and picture shadows use Direct2D's shadow effect (`ModuleKit/DropShadow.cpp`), which
  needs an `ID2D1DeviceContext`: the D2D factory is created as `ID2D1Factory1`
  (`Factories.cpp`); with the 1.0 factory the render targets did not give one.
- Each theme keeps its shadows in `shadows.rc`, included last. The user found the first
  values too faint, then a wide spread "unnatural" (a grown shape reads as a glow): the
  presets are now two layers without spread, a soft key shadow below and a short contact
  shadow (2026-11-27).
- The shadow follows the window's entry and exit animations: `Window` keeps
  `mAnimationOpacity` and the shadow window takes it as its constant alpha
  (`UpdateLayeredWindow` without a bitmap); it used to wait hidden until the animation
  ended and then pop in.
- `Settings::GetLine` returns false when the value comes from a style (`Group`) although it
  fills the buffer: check the buffer, not the result (that hid every shadow set on a style).

### Theme files (layout since 2026-11-25)
- `theme.rc` is a header plus `include` lines; each area has its own file of ~50–150 lines
  (`general.rc`, `styles.rc`, `bar-*.rc`, `popups.rc` = menu skin, `menus.rc`, `notify.rc`…).
- **Order rule.** Plain settings: the FIRST definition wins (`SettingsManager::_FindLine`), so
  `personal.rc` (included by `step.rc` first) overrides everything and a theme value must come
  before a shared default it wants to override. Star lines (`*Popup`, `*HotKey`…), `LoadModule`
  and `<Prefix>On` / `<Prefix>Hide` are read line by line: file order is item order.
- **Styles.** `<Label>Group <Style>`: the label takes every key of the style it does not set
  (`Settings.cpp`; child prefixes follow: `PGroup S` makes `PItemX` fall back to `SItemX`).
  `<Prefix>On` lines of a style ADD to every member's handlers. The engine reads `Group` only
  at the exact prefix a module builds; the checker cannot know those prefixes and tries every
  split of a name, so keep style names from being a prefix of one another (`MMText` vs
  `MMTextCenter`) or it reports names that do not exist.
- **`config\`** is shared and repo-owned (mirrored by deploy, packaged). Menu pieces come in
  two variants (`apps.rc` for `*Popup`, `apps-startmenu.rc` for `*PopupStartMenu`):
  star keys cannot be built from variables and `include` does not work inside a
  `Prefix { }` block. `tools\tests\test_config_menus.py` keeps the variants equal. Icons are
  the theme's: `.icon="$IconExplorer$"` etc. A `$Var$` expands to the FIRST TOKEN of its value
  with the outer quotes removed (`GetTokenW` in `VarExpansionEx`), so quote any value with
  spaces (`IconApp "C:\Program Files\App\app.exe,0"`); unquoted it becomes `C:\Program`.
  A bare key or `""` expands to nothing: no icon.
- **Personal menus** cannot be added through `personal.rc`: it is read before the theme's
  `*Popup !New` lines, and a root-level `*Popup` line that is not `!New` is dropped;
  `*PopupStartMenu` lines there land at the top of the start menu.
- **Check every theme edit** with `tools\rc_effective.py`: dump before, edit, dump after, diff.
  ```
  python tools\rc_effective.py dump --root . --theme "LiteStep Enhanced" > before.txt
  python tools\rc_effective.py diff before.txt after.txt --ignore-new <newstyleprefix>
  ```
  It models includes, first-wins, `Prefix { }` blocks with `@`, star lines, `$Var$` at read
  time (first token, unquoted) and `Group` chains; it refuses `if` blocks and files that are
  not UTF-8. "Equal" means the theme configures exactly the same — check that both dumps are
  not nearly empty (a wrong theme name makes every include miss on both sides).
  `python -m unittest discover -s tools\tests` runs its tests and the config-menu check.

### Theme installation: module map, manifest, plan (since 2026-11-27)
Spec: `docs\superpowers\specs\2026-11-26-theme-install-design.md`. The user wants a theme
that names classic modules to load the nModules instead, and room for a later module that
downloads what a theme still needs. The resolver in litestep is the source of truth; the
manifest and the plan only describe and report.
- **Module map.** `config\module-map.rc` holds `*ModuleAlias <classic-name> <Target>` lines
  (target = DLL name without `.dll`, a folder of `modules\`): `label`, `lsslider` -> Label;
  `popup`, `popup2`, `lslnkmenu` -> Popup; `taskbar`, `taskbar3` -> TaskBar; `vtray` ->
  SysTray; `ckvwm` -> VWM; `shortcut`, `shortcut2`, `jdesk` -> Icon. No alias may target
  `Desk` (§3); `test_module_map.py` checks that, that every target has a project under
  `modules\`, and that no name appears twice. `geekamp`, `lsxcommand`, `textedit` have no
  nModule and are reported missing.
- **First line wins.** `step.rc` includes the map right after `personal.rc`, and the first
  `*ModuleAlias` for a name wins, so a line in `personal.rc` overrides the map;
  `*ModuleAlias popup2 popup2` keeps the original. A theme's own `*ModuleAlias` lines come
  after the map, so they only add names the map lacks. The repo owns the map: deploy and the
  installer overwrite it.
- **Name normalisation** (the same in `ModuleResolver.cpp` and `theme-plan.ps1`, pinned by
  `tools\tests\fixtures\module-names.txt`): file name only (up to the last `\`, `/` or `$`),
  lower case, no `.dll`, no trailing `-` + text that starts with a digit: `label-1.99` ->
  `label`, `textedit-2.4l-13` -> `textedit`, `ls-mute` stays. `taskbar3` and `popup2` have no
  dash, so the map lists both spellings.
- **Resolution** of one module line: no alias -> loaded as written. The line already names
  the target (its file name equals the target and its folder is none, the run folder or a
  `$Variable$`, as in `$LiteStepDir$Popup.dll`) -> as written, so the repo's themes load
  exactly as before. Else `<run folder>\<Target>.dll` exists -> that is loaded ("nModules
  win", even when the original DLL is present). Else the original exists -> the original is
  loaded and logged (fallback: a preference, not a ban). Else the line is skipped and logged.
  A bare name (`label-1.99`) stands for `<run folder>\label-1.99.dll`. Two lines that resolve
  to the same file load it once.
- **Order:** every `LoadModule` line first (so a theme's explicit order, Core first, is never
  disturbed), then `*NetLoadModule`, then `*NetInstallModule` lines. On the Net lines the
  second token is the name; any flags after it are ignored.
- **Classic configuration is not translated.** Swapping the DLL does not make a 2002 theme
  look right: its `*Label`/`*Popup` lines use keys the nModules do not read. Native themes
  are the target.
- **Manifest `themes\<Name>\theme.json`** (optional; every repo theme has one):
  `name`, `version`, `author`, `mode` (`overlay`, the default, or `shell`),
  `requires.modules[]` (`name`, `url`, `sha256`) and `requires.fonts[]` (`name`, `url`,
  `note`). `url` and `sha256` are reserved for the downloader: copied into the plan, never
  fetched. List only fonts Windows does not ship (LiteStep Enhanced: `hooge 05_55`;
  Prototype-X: `Exo 2`). With a manifest, its modules come first and the scan of the
  theme's files (`LoadModule` / `*NetLoadModule` / `*NetInstallModule`, following
  `include`, expanding `$LiteStepDir$` and `$ThemeName$`) only adds what it forgot, with a
  warning. Without one (or with invalid JSON, also a warning) the scan is all there is and
  no fonts are checked. `package.ps1` ships the `overlay` themes.
- **Plan contract** `<run folder>\plans\<Theme>.json` (UTF-8, rewritten on every install,
  switch and deploy of that theme):
  ```json
  { "theme": "LiteStep Enhanced", "generated": "2026-11-27T12:00:00Z",
    "items": [
      { "kind": "module", "name": "label-1.99", "status": "mapped", "resolved": "Label.dll" },
      { "kind": "module", "name": "Notify", "status": "ok", "resolved": "Notify.dll" },
      { "kind": "module", "name": "popup2", "status": "ok", "resolved": "popup2.dll",
        "note": "original used, nModule not installed" },
      { "kind": "module", "name": "geekamp-1.9.7", "status": "missing", "url": null, "sha256": null },
      { "kind": "module", "name": "Desk", "status": "blocked", "reason": "takes over Explorer's desktop" },
      { "kind": "font", "name": "hooge 05_55", "status": "missing", "note": "...", "url": null } ],
    "warnings": [ "theme.json does not list Label, which the theme loads." ] }
  ```
  `name` is the name as the theme wrote it (a `missing` alias target is named after the
  nModule, with `note: "replaces <name>"`); `generated` is UTC. Statuses: `ok` (the DLL or
  font is there), `mapped` (replaced by an nModule that is there), `missing` (needed, not
  there, not mappable), `blocked` (must not load in this mode: `Desk` unless the manifest
  says `shell`). `items` and `warnings` are always arrays. Fonts count as installed when a
  family name is in the HKLM or HKCU `...\Fonts` registry key or in
  `%LOCALAPPDATA%\Microsoft\Windows\Fonts`.
- **Where the downloader plugs in.** A switch runs: plan -> (future: download the `missing`
  items) -> write `theme-select.rc` -> restart. The downloader reads only the plan: it acts on
  `missing` items, writes into the run folder and asks the shell to recycle; it does not need
  to know about aliases, because the resolver maps names at load time. Fetching, `sha256`
  checks and extraction are a follow-up; so are refusing an explicit `Desk.dll` at runtime in
  overlay mode (only the plan blocks it today) and generating the installer's font note from
  the manifests.

### Languages (since 2026-11-26)
- Supported: `en` (default and fallback), `es pt fr de it zh ja` (zh = Simplified for every
  `zh-*`). `lsapi\lsapiInit.cpp` sets `SystemLanguage` from the first preferred UI language
  (display language, not the regional format); `step.rc` sets `Language $SystemLanguage$`
  after `personal.rc`, so `Language ja` in personal.rc forces one (handy for testing; remove
  it after). A regional code there (`pt-BR`) works: when an included file such as
  `lang\pt-BR.rc` does not exist, lsapi reads `lang\pt.rc` (`BaseLanguageFile` in
  `SettingsFileParser.cpp`; `rc_effective.py` and Core's month names do the same).
- Texts: `lang\<code>.rc`, one `Txt<Name> "<text>"` per text (76 today), included by step.rc
  before the theme, English after it (a missing text shows in English). Themes and
  `config\menus` write `"$TxtExplorer$"` etc., never literal words; product names
  (Word, Firefox…) and CPU/GPU/RAM/VRAM are not translated. `config\texts.rc`, included after
  the theme, gives every theme the modules' own texts (loading row, favourites).
- Adding a text: a row in every `lang\*.rc` (same keys everywhere — `test_lang.py`). Adding a
  language: a new `lang\<code>.rc`, the code in `lsapiInit.cpp`'s list, the C runtime locale
  in `Core\TextFunctions.cpp`, and `CODES` in the test.
- Clocks: each theme keeps its date format; `[Time]` uses the locale of `$Language$`, so
  `%B %A %b %a` follow it (Core reads it on load: a language change needs a recycle).
- Fonts: a theme's `lang\<code>.rc` (only the languages that need one) may override the
  theme's own keys; LiteStep Enhanced's zh/ja set `MMFont`/`MMFontSmall`/`MMFontSize`/
  `MMTextAA` (the pixel font has no CJK glyphs). The clock keeps `MMClockFont` and its panel
  stays aliased. Themes on Segoe UI rely on DirectWrite's font fallback.
- StartMenu names a content entry's settings after what it lists (`PopupStartMenuPrograms`,
  `…Recent`, `…Computer`), not after its title, which is translated.
- zh and ja texts were written by the implementer; have a native speaker review them.

---

## 6. Source changes made (and why) — do not regress these

All are in `modules/` unless noted. Run `git diff` for exact hunks.

- **TaskBar layout bug (fixed):** `TaskBar/Taskbar.cpp` `Relayout()` computed
  `lines = floorf(...)` which became **0** when the taskbar is shorter than one button row,
  making `buttonSize` negative so every task button rendered at width −2 (invisible). Fix:
  `lines = std::max(1.0f, lines);` in **both** the horizontal and vertical branches.
  Lesson: the bug looked like a shell/overlay/monitor problem; instrumenting the data path
  (AddExisting → AddWindow → AddTask → Relayout) found it in one pass. Instrument, confirm
  with a refutable observation, fix the cause, remove scaffolding.
- **Global UPPERCASE:** `ModuleKit/WindowSettings.{hpp,cpp}` add a `bool textUppercase`
  (read from `TextUppercase`, with RC default); `ModuleKit/Window.cpp` applies `CharUpperW`
  in the non-evaluate branches of `SetText`/`UpdateText`.
- **Now-playing:** `Core/MediaControl.cpp` (split out of `TextFunctions.cpp`, which now only
  has the time and system-meter functions) adds a `[NowPlaying]` dynamic function backed by
  a background thread using **GSMTC** (`winrt Windows.Media.Control`), formatting
  `"Artist - Title"`. `Core/ModuleEntry.cpp` fires `DynamicTextChangeNotification(L"NowPlaying",0)`
  in `WM_TIMER`. `Core/Core.vcxproj` needed `LanguageStandard=stdcpp17`,
  `#pragma comment(lib,"windowsapp.lib")`, and `_ENFORCE_MATCHING_ALLOCATORS=0`.
- **GPU meters (Core):** `Core/GpuMeter.cpp` adds `[GPU]`, `[VRAM]` (percent) and
  `[VRAMUsedGB]`, next to the existing `[CPU]` / `[Memory]`. A thread reads the PDH
  counters Task Manager uses (`\GPU Engine(*)\Utilization Percentage`, busiest engine;
  `\GPU Adapter Memory(*)\Dedicated Usage`) for the adapter with the most dedicated
  memory (DXGI; with integrated plus discrete graphics, that is the discrete card). The theme shows CPU/GPU/RAM/VRAM
  as `MMeter*` overlay labels in the dark box of `injektion.png` (x 180..287, y 1..18)
  that the original used for a quick launch.
- **Media control (Core):** `Core/MediaControl.cpp` — the GSMTC thread also runs queued
  transport commands and tracks the play position. Bangs: `!MediaPlayPause`, `!MediaPlay`,
  `!MediaPause`, `!MediaStop`, `!MediaNext`, `!MediaPrev`, `!MediaSeek <0-100>`,
  `!MediaVolume <0-100 | +n | -n>` (master volume via `IAudioEndpointVolume`). Exports
  `MediaGetPermille(what)` / `MediaSetPermille(what, permille)` (0 = volume, 1 = track).
  Do **not** use `!KeyCombo MEDIA_*` for player buttons: simulated media keys are routed
  by explorer.exe and go nowhere when litestep is the shell.
- **Media player rotation (Core):** `*MediaPlayer <Name> "<exe | shell:AppsFolder\id>" [match]`
  lines list the players in rotation order; only installed ones are kept (several lines
  with one name = alternative paths). The selected one decides which media session the
  buttons, the track slider and `[NowPlaying]` follow (GSMTC session whose
  `SourceAppUserModelId` contains the match text; classic Winamp is driven through its
  `Winamp v1.x` window instead). `!MediaPlayerNext` / `!MediaPlayer <name>` select and run
  `MediaPlayer<Name>Command`; the theme uses `!nSetImage MPlayerIcon ...` there to swap the
  icon (`player_winamp|spotify|tidal.png`, 21x17, drawn on the panel's own background).
  `!MediaPlayerLaunch` opens it; pressing play with the player closed opens it too. The
  choice is stored in `HKCU\Software\LiteStepTNG\MediaPlayer`. The start-up command runs on
  Core's first timer tick, because the labels do not exist while Core loads. The rotation
  was verified with stand-in paths and the controls against one real player (TIDAL); the
  Winamp and Spotify control paths were not run against the real programs.
- **Slider labels (Label):** `<Prefix>Slider Volume|Track` + `<Prefix>SliderHandle <label>`
  turn an nLabel into a slider; the handle is normally an overlay child with
  `ClickThrough true`. It polls Core every 500 ms, click/drag sets the value (track seek
  only on release). `Label/ModuleEntry.cpp` also registers the standard window bangs as
  `!nLabelMove|Show|Hide|SetText... <label> [args]`.
- **Virtual desktops (VWM):** `VWM/ModuleEntry.cpp` was an empty stub and did not export
  its entry points. It now parks the windows of other desktops 20000 px to the right and
  tags them with the window property `LSVWMDesk`; it gathers everything on load and on
  unload so a recycle never strands a window. Bangs `!VWMDesk <n>`, `!VWMNext`,
  `!VWMPrev`, `!VWMGather`, `!VWMSend <n> [hwnd]` (sends the active window, or the given
  handle, to desktop n without following it; the theme binds it to right-click on the
  1-4 buttons and Win+Shift+1..4, which reach nHotKey through its keyboard-hook fallback
  because Explorer owns those combinations); `VWM/Transition.cpp` is the switch effect
  (`VWMTransition`, `VWMTransitionDuration`, `VWMTransitionDistance`): a screen capture of
  each monitor's work area shown in a topmost click-through layered window, which slides
  and fades after the switch — other programs' windows are never animated. Capturing
  costs about 40 ms per monitor before the switch starts. Parking stores the window's
  original x in the `LSVWMHomeX` property and un-parking restores that absolute position:
  the moves are asynchronous, and "current position minus the offset" stranded slow
  (Electron) windows 20000 px off screen, untagged, when two switches came back to back —
  they then showed up on the wrong desktop still tagged as parked, and clicking them
  switched back. `Gather` (on load and `!VWMGather`) also rescues untagged windows found a
  whole number of offsets away; RC `VWMDesktops`, `VWMDesk<N>Command`. Activating a parked
  window (taskbar, Alt+Tab) switches to its desktop. Windows' own virtual desktops need
  explorer.exe and are not available.
- **Pinned apps popup:** `!PopupPinned` (content source `PINNED` in `Popup/ContentPopup.cpp`)
  lists the Windows taskbar pins: the `.lnk` files in
  `%APPDATA%\Microsoft\Internet Explorer\Quick Launch\User Pinned\TaskBar` plus packaged
  (Store) apps, which have no shortcut and are found by scanning the binary
  `HKCU\...\Explorer\Taskband\Favorites` for `PackageFamilyName!AppId` at both UTF-16
  alignments. Packaged apps are named/iconed via `shell:AppsFolder\<id>` and launched with
  `explorer.exe "shell:AppsFolder\<id>"`. Reloaded on every open (content popups are dynamic).
- **Favorites (supersedes the read-only taskbar list above):** `!PopupPinned` now lists
  `$LiteStepDir$favorites\`, LiteStep's own folder of shortcuts (`namespace Favorites` in
  `Popup/ContentPopup.cpp`). It is created on first use and seeded with the Windows taskbar
  pins (copied `.lnk` files, plus a generated shortcut per packaged app via
  `IShellLink::SetIDList`). Right-clicking a file-backed `CommandItem` shows a system
  context menu: "add to favorites" (copies the file there) or, for a favorite, "remove"
  (deletes it). Menu texts: `nPopupPinText` / `nPopupUnpinText`. `Popup::SuspendAutoClose`
  keeps the popup open while that system menu is up. Windows itself offers no API to pin
  to its taskbar, hence a separate list.
- **All applications:** `!PopupApps` (source `APPS`) lists the shell's Applications folder
  (`FOLDERID_AppsFolder`), the only place that includes packaged/Store apps; a good part
  of those (Calculator, Terminal...) have no Start Menu shortcut and are
  absent from `!PopupPrograms`. Items launch with `explorer.exe "shell:AppsFolder\<name>"`
  and can be added to the favorites (`Favorites::AddApp` writes a shortcut by IDList).
  First open costs ~2.5 s of icon extraction; the popup is then kept loaded (47 ms).
  The theme has no entry for it: the user had it removed (too long for a
  popup), so the bang exists but no menu uses it.
  The theme's JUEGOS submenu is the `games\` folder of the run folder (shortcuts: `.url` with
  `steam://rungameid/<id>` for Steam, `.lnk` to `shell:AppsFolder\<id>` for Store apps, which
  then show the app's own icon), so games are added or removed there, not in the theme.
- **Content popups load off the UI thread:** reading a folder and extracting its icons on
  litestep's main thread stalled the whole system's mouse (the low-level mouse hooks of
  nHotKey and the popups run on that thread). `Popup/ContentLoader.cpp` reads any source
  except `THEMES` into plain `ContentEntry` records (strings, extracted `HICON`s, ID lists of
  folders to watch) on a worker thread (own STA, pins its module); `ContentPopup` shows a
  `LoadingItem` meanwhile (info-row skin, `<Popup>LoadingText`, the theme sets `CARGANDO`, and
  a spinner stepped by `FrameClock`, both hidden for the first 150 ms) and builds the items
  when `LiteStepTNG.PopupContentLoaded` arrives. A generation number drops results of loads
  that were closed, unloaded or destroyed; kept popups (`keepLoaded`) finish loading while
  hidden. The arrival must run under `Window::UpdateLock`: without it every item added to a
  visible popup repainted it synchronously (800 ms for 20 items). Measured: opening
  `!PopupApps` cold blocked the main thread 1757 ms before, 93 ms after; System32 (~5000
  items) still blocks once for ~0.5 s while its item windows are created.
- **`!PopupFolder:"path"` with spaces:** the rest of the line is the path; the parser used
  to read a piece of it as the settings prefix, which corrupted the whole root popup.
- **Content popup caching:** content popups are "dynamic": `PostClose` used to free every item
  (and so every cached icon) and the next open re-enumerated and re-extracted everything
  (~300 ms for Programs). `ContentPopup::keepLoaded` now keeps the Programs / Start Menu
  popups and any path popup under the Programs folders in memory (28 ms on later opens);
  delete/rename notifications mark them `stale` and they rebuild on the next show.
  `!PopupRecentDocuments` lists only the newest `MaxRecentItems` (default 20) shortcuts by
  modification time, newest first, instead of the whole Recent folder (hundreds of entries on a used system).
- **Notifications (new module `modules/Notify`, `Notify.dll`):** shows the Windows toast
  notifications as boxes stacked top-down on the right edge of the work area.
  `Listener.cpp` polls `UserNotificationListener` (WinRT, works unpackaged here; its
  `NotificationChanged` event does not, hence the polling) on an MTA thread and posts
  added/removed ids to the module window. Each `NotificationBox` is a top-level Drawable
  (prefix `NotifyBox`) with child parts `Title` (app), `Text`, `Time`, `Close`; the X posts
  `WM_NOTIFY_DISMISS` and, with `NotifyDismissFromSystem true`, also removes the toast from
  the Windows notification centre. Layout/overflow is in `ModuleEntry.cpp`
  (`Relayout`; `NotifyOverflow DropOldest|Wait` decides what happens when the column is full).
  The close button's picture is 10x10, so the box also has an invisible `CloseArea` part
  (34x20 in the theme) that closes too: a near miss used to land on the box, which opens
  the program and closes the box, and looked as if the X had opened the program. Clicks
  can be tested without the mouse by posting `WM_MOUSEMOVE`/`WM_LBUTTONDOWN`/`WM_LBUTTONUP`
  with client coordinates to the box window. Bangs `!NotifyTest ["app" ["title" ["text"]]]` and
  `!NotifyClear` test it without the mouse. Clicking a box goes to its app
  (`AppActivation.cpp`: finds a window by AppUserModelID, else launches
  `shell:AppsFolder\<id>`; a toast's own action cannot be invoked from outside) and, with
  `NotifyClickDismisses true`, closes it. `NotifySound` is a .wav played on arrival
  (`themes\LiteStep Enhanced\sounds\notify.wav`, generated). With Windows in
  "Do not disturb" there is no Windows banner or sound, but toasts still reach the listener,
  so the boxes can replace the banners. To raise a test toast from a script, use the
  AppUserModelID of an app whose notifications are enabled in Windows (e.g. Terminal's). Private messages must stay clear of the `LM_`
  range, which starts at `WM_APP` (32768). Needs `stdcpp17` like Core.
  Boxes are animated: they enter with `NotifyBoxShowAnimation*` (slide in from the right,
  `ShowAnimationOffsetX`), leave with `NotifyBoxHideAnimation*` (`Window::HideAnimated`; a
  closing box moves to `sClosing` and is deleted on `WM_NOTIFY_CLOSED` when the animation
  ends), and the others close up with `Window::SetAnimation` (`NotifyMoveDuration`, ms).
  Every theme loads it and styles it in its own `notify.rc` (LiteStep Enhanced with the
  popup frame images, the others with colours, `CornerRadius*` and a Segoe Fluent Icons X,
  U+E711, as the `Close` part's text). `NotifyTimeFormat` is formatted without the theme's
  language, so month and day names come out in English: keep it numeric.
- **Window frames (new module `modules/Frames`, `Frames.dll`, experimental):** covers the
  native title bar of other programs' windows with a skinned one (prefix `Frame`, parts
  `FrameTitle|Min|Max|Close`, `Active*` keys for the foreground window). Nothing is
  injected: WinEvent hooks (out of context, narrow ranges) report moves, shows, title and
  z-order changes, and each bar is re-stacked right above its target (it is not made an
  owned window, which would tie the shell's input queue to the other program). Only
  windows with a standard caption get one: `WindowFrame::GetNativeBarRect` skips those whose
  client area starts above the caption's bottom (browsers, Terminal, Electron, Store apps).
  Drag = `WM_NCLBUTTONDOWN/HTCAPTION` posted to the target, buttons = `WM_SYSCOMMAND`,
  right click = system menu. Known limits: the bar trails a dragged window, elevated
  windows ignore its clicks, borders are not skinned. `!FramesToggle`, `*FramesExclude`.
  Since 2026-11-23 the look is the popup frame around the whole window: `WindowFrame::Body`
  (prefix `FrameBody`, `windowframe.png` = `popupframe.png` with a 31px top edge) is a
  second top-level window drawn OUTSIDE the target (`FrameBodyInset*`), with the content
  cut out by a window region and `WS_EX_LAYERED|WS_EX_TRANSPARENT` so clicks reach the
  target's native resize borders; the interactive bar is transparent on top of it. The bar
  moves the target itself (`SetWindowPos` while the button is down): posting
  `WM_NCLBUTTONDOWN` does not work, the mouse stays over our bar. Minimize/maximize are
  only offered when the target has the matching style (forcing SC_MAXIMIZE on a fixed-size
  dialog ignores the work area). Z-order: activation raises the target above its bar, and
  top-level reorders are reported for the desktop (not OBJID_WINDOW), hence the settle
  passes and the 500 ms watchdog.  Maximized: the native caption is 23px, not 31 (the frame's top hangs off the screen), so
  the decoration shrinks to the strip over the caption, laid out from the window's own
  corner; bar and decoration share one `outer` rect and the theme positions title/buttons
  relative to it. Skipped: windows of a higher integrity level (elevated; `SetWindowPos`
  and posted commands are refused, and a posted test message "succeeds" anyway), and
  full-screen windows (frame covers the monitor without `IsZoomed`). To test without the
  mouse: a WinForms window in its own powershell process, driven with `ShowWindow` /
  `SetWindowPos`.  Verified without the mouse (bar appears, follows a move, hides on minimize, stacks
  correctly between two overlapping windows); drag, buttons and the Active look were not.
- **Desktop icons (Icon module, first use in this build):** the theme's `MIcons` group shows
  the Desktop folder over the whole work area of the primary monitor, filled from the top
  right corner downwards (`MIconsX/Y/Width/Height` choose the area: the whole desktop, as
  the user wants it, or just a strip; `Start TopRight`,
  `PrimaryDirection Vertical`), in the theme's font. Three things were added for running
  on top of Explorer: a generic window setting `<Prefix>OnDesktop true` in ModuleKit (the
  window is kept at the bottom of the stacking order, which is right above the shell's
  desktop; implies not topmost); `<Prefix>ReplaceExplorerIcons true`, which hides
  Explorer's own icon view (`SysListView32` under `SHELLDLL_DefView`) while the group is
  visible and shows only what Explorer would (files of the user and common Desktop
  directories, system objects enabled under `HideDesktopIcons`); and `!DesktopIcons
  [show|hide|toggle]`. `MIconsHidden true` (e.g. in personal.rc) turns it off.
  `<Prefix>IconOffsetTop` leaves room above the icon inside its tile. Dragging is in
  `Icon/TileGroupDrag.cpp`: a tile that is pressed and moved posts a message to its group,
  which runs `SHDoDragDrop` with the folder's `IDataObject` (drag out to folders, programs,
  the Recycle Bin); the group is a drop target that hands foreign drops to the folder's
  own `IDropTarget` and, for one of its own icons, only notes the point and returns
  `DROPEFFECT_NONE` — returning MOVE there would let the drag source delete the files.
  Dropping an icon on another slot swaps them; slots are kept per icon (parsing name) in
  `HKCU\Software\LiteStepTNG\IconOrder\<prefix>`. Only the remembered-order loading was
  verified without the mouse (write a slot value, reload); the gestures were not. The drag
  carries a picture of the item (`IDragSourceHelper::InitializeFromBitmap` with the image
  from `IShellItemImageFactory`). Core's `FileSystemLoader` asks that same factory for a
  content preview first (`SIIGBF_THUMBNAILONLY`: pictures, videos, documents) and falls
  back to the icon paths; shell thumbnails can come with an all-zero alpha channel, which
  `Utilities/BitmapAlpha.h` repairs. A folder kept in the cloud (a OneDrive Desktop)
  never reports `SHCNE_CREATE`/`DELETE` for its files, only `SHCNE_UPDATEITEM` for the
  folder itself, so on a folder-level notice the group rescans and diffs (`Rescan`,
  debounced 400 ms). Found with a temporary file log in `HandleChangeNotify`. An "item
  updated" notice used to clear the tile's picture and load nothing (`Tile::UpdateIcon`
  was a stub), so the pictures vanished one by one and only the captions stayed; it now
  reloads the picture and keeps the old one until the new one arrives (`ReloadThumbnail`,
  `ItemLoaded` for an existing tile). Reproduce without touching files:
  `SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, <path>)` and `SHCNE_ASSOCCHANGED`. The module
  crashed on load before: `TileGroup::mWorkingFolder` was not initialised and `SetFolder`
  released it. **A module that faults in `initModuleW` is unloaded but its windows remain,
  so litestep dies a moment later with the fault in `<Module>.dll_unloaded`** — and then
  Explorer's taskbar and icons stay hidden. When deploying a module that has not run
  before, check that litestep is still alive afterwards and be ready to disable the module
  in the deployed theme.rc.
- **Wallpaper (Wallpaper module):** the wallpaper is Windows' own, drawn by Explorer.
  `ThemeWallpaper "<file>"` (+ `ThemeWallpaperStyle`) gives a theme its picture, applied on
  load; a theme without it leaves the user's choice alone. When a theme's picture goes up,
  the one the user had is remembered (`HKCU\Software\LiteStepTNG`: `UserWallpaper`,
  `ThemeWallpaperApplied`) and comes back when a theme without a wallpaper loads, unless
  the user has picked another meanwhile. Wallpapers are set with
  `SystemParametersInfo(SPI_SETDESKWALLPAPER, ..., SPIF_UPDATEINIFILE|SPIF_SENDCHANGE)` on a
  worker thread; the old code wrote the registry value and broadcast by hand, which makes
  Explorer redraw nothing. Verify through `SPI_GETDESKWALLPAPER` while switching themes.
- **Entry/exit animations (ModuleKit):** `<Prefix>ShowAnimationDuration|Offset|OffsetX` (fade
  and slide in when a hidden window is shown) and `<Prefix>HideAnimationDuration|Offset|OffsetX`,
  used by `Window::HideAnimated(done)` (fade and slide out, then hide and call done; without a
  duration it hides at once). Both run on `FrameClock`.
- **The tray follows its icons (SysTray, `Tray.cpp`):** `OverflowAction Size*` used to grow
  the tray only past the size the theme gave it. `<Tray>MinWidth` / `MinHeight` (default:
  that size, so other themes keep growing only) is now the floor: LiteStep Enhanced sets
  `MTrayMinWidth 72`, so its tray grows left with each icon and shrinks when one goes, the
  right edge fixed, and draws `systraydark.png` in `Edges` mode (left 52, right 10) so only
  the dark box stretches. Glass, Windows-nModules and Prototype-X keep grow-only: their tray
  sits next to the taskbar inside the bar, and shrinking would leave a gap. Checked with
  three `NotifyIcon`s from a test process: 212 px, 254 px with them, 212 px after.
- **A taskbar on every monitor (TaskBar, `ModuleEntry.cpp`):** `<Taskbar>EveryMonitor true`
  (every theme sets it) with more than one monitor keeps the taskbar on the primary
  monitor's windows (`Monitor 0` unless the theme names one) and creates a copy
  `<Taskbar>Screen<n>` per other monitor (MonitorInfo numbering, 0 = primary), which lists
  only that monitor's windows: the old `Monitor` filter, with `LM_MONITORCHANGED` moving a
  button when its window changes monitor. Before creating a copy the module writes, with
  `LSSetVariable`, `<copy>Group <Taskbar>` (it inherits every key, Button and Thumbnail
  children included), `<copy>Monitor <n>` and `<copy>X/Y/Width/Height` in pixels: top-level
  windows measure percentages against the primary monitor, so the copy's geometry is worked
  out against its own monitor here (`<Taskbar>ScreenX/ScreenY/ScreenWidth/ScreenHeight`
  override it; Glass, Windows-nModules and Prototype-X use the full width, since their
  primary bar leaves room for a start button and a tray). Explorer's secondary taskbars are
  hidden by nCore, so the other monitors had no taskbar before. With
  `<Taskbar>ReserveScreen` (default true) a 1 s timer keeps each copy's edge out of its
  monitor's work area (`SPI_SETWORKAREA` without broadcast, as Core does for the primary;
  restored with a broadcast on unload). `NCORE_DISPLAYCHANGE` with a different monitor
  layout rebuilds every taskbar. Window positions are now rounded with `floorf`: `int(x+0.5f)`
  truncated toward zero, so a bar on a monitor left of the primary started 1 px late.
  Verified on three monitors: a window moved from the left monitor to the primary left the
  left copy's bar and appeared on the primary one; the secondary work areas shrank by 26 px.
- **Minimized windows as thumbnails (TaskBar, `MinimizedShelf.cpp`):** with
  `MinimizedShelfEnabled true` (every theme sets it, in `minimized.rc`) each minimized task
  window gets a small `OnDesktop` window (prefix `MinimizedShelf`) right above its own task
  button (below it for a taskbar in the top half of the monitor), showing a DWM thumbnail of
  its content: DWM keeps a minimized window's last frame. As wide as the button within
  `MaxThumbWidth`, at most `ThumbHeight` high, `Gap` from the button, `Padding` around the
  content. A first version put them all in one row above the taskbar; the user wanted each over
  its task. The thumbnails follow their buttons (a 120 ms timer while any exist, since the
  taskbar slides its buttons); `WindowManager::GetButtonRect` gives the button.
  Minimize/restore come from `EVENT_SYSTEM_MINIMIZESTART/END` (out of context); windows
  minimized earlier are picked up after `AddExisting` and in the maintenance pass; windows
  parked by nVWM are skipped. `GetMinRect` answers with the thumbnail's rectangle, so the
  minimize animation flies into it. A click restores the window (`ShowWindowAsync(SW_RESTORE)`
  + `ForceForeground`). Verified on screen: Explorer and WhatsApp, minimized, each showed its
  content over its own button. Windows' window animations were off on this machine; the user
  had them turned on (Performance options) on 2026-11-27.
  **Animated:** a thumbnail grows out of its button when its window is minimized and shrinks
  back into it when the window returns. The window's `ShowAnimation*`/`HideAnimation*`
  (themes: 220/180 ms, offset 10 toward the button, -10 in Prototype-X whose taskbar is at
  the top) fade and slide the background; `Thumb::ApplyThumbnail` scales the DWM content
  (55 % to 100 %, pinned to the button's side) and its opacity on the same curves, driven by
  `Window::RequestFrames`. A restored window's thumbnail moves to `mClosing` and is deleted
  by the 120 ms timer once `IsGone()`. The exit starts about 180 ms after the restore:
  `EVENT_SYSTEM_MINIMIZEEND` arrives when Windows' own restore animation ends.
  **Desktop layers:** the icon group covers the whole desktop and is OnDesktop too, and each
  OnDesktop window used to restack itself right above the desktop, so the icons and the
  thumbnails took turns at the bottom and every other click on a thumbnail landed on the icons
  (found with a read-only `WH_MOUSE_LL` monitor logging the window under each press and
  LiteStep's capture). `<Prefix>DesktopLayer <n>` (default 0, read in `Window::Initialize`,
  kept as the window property `LSDesktopLayer` = n+1) keeps an OnDesktop window above every
  OnDesktop window of a lower layer; the themes set `MinimizedShelfDesktopLayer 1`.
  `DesktopInsertAfter` climbs from the background over shadows, hidden windows, the window
  itself and lower layers. Check without the mouse: minimize a window of your own process and
  compare the z-order positions of `MinimizedShelf` and `MIcons` while restacking each.
  **Lost mouse capture:** a press on the icon group starts a rectangle selection that holds
  the capture, and the right press that opens the menu is one. A capture that ended without
  its release (taken by another window, a drag) left `Window::mCaptureHandler` set and the
  selection open, so the next click (the first one on the menu or a thumbnail) went to the
  icons. Now `Window` clears the handler on `WM_CAPTURECHANGED`, `TileGroup` cancels the
  selection on it (and a press during a stale selection starts a new one), and `Popup::Show`
  releases a capture held by any non-popup window. Checked without the mouse
  (`WM_RBUTTONDOWN` posted to MIcons, then `!PopupContext`, read `GetGUIThreadInfo`).
- Build with `/nr:false` when waiting for MSBuild to finish: `/m` leaves worker nodes
  (`MSBuild.exe`) running for reuse, so "wait until MSBuild exits" never ends. Never pipe
  MSBuild's output through a command that can be moved to the background: when nobody reads
  the pipe any more, the `cl.exe` processes block writing to it and hang with no CPU. Write to
  a log (`/flp:LogFile=...`) and start it with `Start-Process -PassThru` + `WaitForExit(ms)`.
  For a fix in a few modules, build only their `.vcxproj` (ModuleKit first,
  `/p:SolutionDir=<repo>\ /p:BuildProjectReferences=false`): seconds instead of minutes. A
  change in a ModuleKit header recompiles every module; a change in a `.cpp` only relinks.
- **Taskbar animation:** `<Taskbar>AnimationDuration` (ms) slides task buttons to their new
  place when tasks are added/removed (`TaskButton::SetTarget/StepAnimation`, timer in
  `Taskbar`). The thumbnail's own `ThumbnailAnimate` and the popup `ShowAnimation*` settings
  are theme-only.
- **Animations paced by the display (`ModuleKit/FrameClock`):** every animation (window
  entry `ShowAnimation*`, `SetAnimation` moves, the taskbar slide, the VWM transition) steps
  once per refresh. While anything is subscribed, a thread waits on `DwmFlush()` and posts
  the registered message `LiteStepTNG.FrameTick` (`FrameClock::Message()`); a window has at
  most one tick queued and must `FrameClock::Ack` it, so a busy UI thread drops frames
  instead of queueing them. No subscribers = no thread. `Window::RequestFrames/StopFrames`
  forward ticks to a `MessageHandler`; `LSModule::DeInitalize` stops the clock. New
  animations should use this, not `SetTimer` (rounded to ~15.6 ms), and measure progress
  with `StopWatch` (QPC), not `GetTickCount64` (same 15.6 ms steps). Before, moves repainted
  in a loop blocked on vsync inside `EndDraw`. Measured on a 74 Hz monitor: 13.3–13.5 ms
  per step, nothing posted once the animation ends.
- **Classic module names load nModules (litestep, `ModuleResolver.{h,cpp}`,
  `ModuleManager.cpp`; 2026-11-27):** `ModuleManager::_LoadModules` reads the `*ModuleAlias`
  lines (`_ReadAliases`, first per name wins), then queues the `LoadModule` lines, then the
  `*NetLoadModule` and `*NetInstallModule` lines (before, `SettingsFileParser.cpp` only logged
  those). Every location goes through `_MakeModule` -> `ModuleResolver::Resolve` (rules in §5
  "Theme installation"); `_QueueModule` drops a second line that resolves to the same file.
  Each substitution, fallback and skip is written with `Logger::Log` (`Modules: ...`).
  `ModuleResolver` uses only the standard library, so `tools\tests\module_resolver_test.cpp`
  builds it with g++ or `zig c++` (`test_module_resolver.py`, skipped without a compiler).
  `tools\tests\fixtures\theme-classic-demo\` is a scratch theme (`*NetLoadModule label-1.99`)
  for checking it in a running shell; `smoke-test.ps1` counts aliased modules among the
  ones it expects loaded.
  **Known limitation:** every nModule needs Core.dll (nCore). A classic theme has no line for
  it, and the loader adds no module the theme does not ask for, so such a theme gets a modal
  "There was a problem connecting to nCore!" from Label (seen while testing; the fixture
  loads `$LiteStepDir$Core.dll` itself for that reason). Loading Core implicitly when an
  aliased nModule is loaded is an open follow-up.
- **StartMenu `.icon=` parsing:** `StartMenu/ModuleEntry.cpp` now parses a leading
  `.icon="file,idx"` token on menu entries (previously only nPopup did), passing the icon to
  Command/Folder items.
- Other touched files (see `git diff --stat`): `litestep/litestep.cpp`,
  `Popup/ContentPopup.*`, `Popup/ModuleEntry.cpp`, `SysTray/Tray.cpp`, `HotKey/ModuleEntry.cpp`,
  `lsapi/*`, `step.rc` — supporting the above and theme wiring.

---

## 7. Gotchas / lessons

- **Read the source before writing a feature.** The popup date/time box needed no code — the
  Popup module already evaluated `[Time]` in InfoItems. Grep first.
- **Aspect ratio differs from the preview.** The reference is 1024×768 (4:3); development
  was done on an ultrawide (21:9) monitor. Element positions use `100%`-relative coordinates, so judge the match
  by relative placement (edges/corners) and styling, not by pixel-identical coordinates.
- **Build logs** (`build*.log`) and `step.rc.bak-*` in the working tree are throwaway; don't
  commit them.
- The user authorized source changes to achieve parity ("Si necesitas cambiar el source
  hazlo"). Prefer a robust source fix over a theme-only workaround when the bug is in the
  engine (e.g. the `lines=0` clamp vs. just resizing the bar).

---

## 8. Known remaining / open items

- **UWP apps (Settings, Calculator, `ms-settings:` links) do not open with litestep as
  shell — RESOLVED 2026-11-21 by making Explorer the shell (§3); kept for the record.** The popup launches them fine (`SystemSettings.exe`
  starts) but the `ApplicationFrameWindow` stays DWM-cloaked (`DWMWA_CLOAKED` = 2, shell
  cloak); only explorer's immersive shell uncloaks it. Classic Win32 and packaged-Win32
  apps (Control Panel, Snipping Tool, Terminal, Notepad) open normally. What was tried:
  - Uncloaking from another process: `DWMWA_CLOAK` → access denied; `WCA_CLOAK` → no effect.
  - Explorer started in shell mode next to litestep (temporary HKCU `Shell` flip): taskbar
    and Progman appear, but no UWP host, because litestep owns the shell window.
  - `LSSetAsShell false` in step.rc + bare `explorer.exe` (no registry change needed;
    explorer then takes the shell window): frames uncloak but apps hang on the splash, the
    `CoreWindow` never attaches to the frame. Same or worse with `LSDisableTrayService`.
  - Explorer alone (litestep not running): Calculator loads fine.
  So litestep's presence in shell mode blocks explorer's immersive shell; the cause inside
  litestep is not identified (not the shell window, not the tray service). Untested next
  step: explorer as the real Winlogon shell with litestep in overlay mode.
  **Workaround used then (replaced on 2026-11-25 by `ms-settings:` pages in
  `config\menus\windows-settings.rc`, now that Explorer is the shell):** the config menus used classic Win32
  panels instead of `ms-settings:` — `control.exe` + `sysdm.cpl`, `mmsys.cpl`, `ncpa.cpl`,
  `timedate.cpl`, `intl.cpl`, `appwiz.cpl`, `powercfg.cpl`, `main.cpl`, `firewall.cpl`,
  `/name Microsoft.NetworkAndSharingCenter`, `/name Microsoft.UserAccounts`, `sndvol.exe`,
  `rundll32 sysdm.cpl,EditEnvironmentVariables`, and the `explorer.exe shell:::{GUID}`
  folders for Devices and Printers / Desktop Background. All verified to open a real
  window on Windows 11. `desk.cpl` redirects to the Settings app and `bthprops.cpl`
  errors, so Display, Bluetooth and Windows Update have no working entry.
  `litestep.exe !Bang` from a command line does not reach the running instance here
  (its `FindWindow` for the main window fails), so it cannot be used to test bangs.
  Gotcha: `explorer.exe ms-settings:` launchers linger as stuck processes. (Shell mode
  and its start-up prompt were removed on 2026-11-26, §3.)
- **Do not synthesize mouse input while the user is at the machine** (they are usually
  using it): a test right-click landed on their browser. Verify with `WindowFromPoint` that
  the target belongs to litestep, and prefer screenshots of what the user already has open.
- Click paths of the 2026-11-21 media/slider/VWM work (player buttons, slider drag, desktop
  switching) were built and load correctly but were only verified passively (knob follows
  the track and the volume); the user tests the clicks.
- `modules/Popup/ContentPopup.cpp` has a `TODO(human)` in `FormatThemeTitle` (how to mark
  the active theme in `!PopupThemes`); the lslnk popup title position inside the white
  header box (`MMMenuLnk*` in theme.rc) was not visually verified.

- **Theme installation follow-ups** (§5 "Theme installation", §6 resolver): a classic theme
  without `LoadModule ...Core.dll` gets nCore's connection error from every nModule; the
  downloader of `missing` plan items; refusing `Desk.dll` at runtime in overlay mode;
  translating classic module configuration is a non-goal.

- Full side-by-side screen-vs-preview parity was still being tightened at last session end;
  the structural match is complete (all elements present, placed, and styled). Residual
  differences are PNG-skin micro-details, not layout or function.
- `modules/TaskBar/Taskbar.cpp` (the `lines` clamp) is the only code change ready to commit
  on its own merits; the rest are theme assets/config under `themes\LiteStep Enhanced\`
  plus the feature source above.
