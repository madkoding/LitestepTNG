# AGENTS.md — LiteStepTNG

Context guide for AI agents (and humans) working on this repo. Read this first so you
don't re-derive what previous sessions already learned.

---

## 1. What this project is

LiteStepTNG is a modern x64 port/evolution of the classic **LiteStep** Windows shell
replacement, written in **C++14/17** with **Direct2D/DirectWrite** rendering. It can run
as the Windows shell (replacing `explorer.exe`) or as an overlay.

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
  HotKey/ VWM/ Desk/ Icon/ Panels/ Wallpaper/ MediaInfo/ Calendar/ TaskSwitcher/
  External/        third-party (v8, etc.)
utility/           logger etc.
bin/Release_x64/   build output (DLLs + litestep.exe)
themes/            (working copy of themes; the RUNTIME uses C:\LiteStepRun — see §3)
```

---

## 2. Build & deploy

**This repo builds here (`C:\LiteStepTNG`) but RUNS from `C:\LiteStepRun`.** They are two
separate locations. Building does not update the running shell — you must copy DLLs over.

### Build (PowerShell)
```powershell
$msb = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
& $msb "C:\LiteStepTNG\litestep.sln" /p:Configuration=Release /p:Platform=x64 /m /verbosity:minimal /nologo
# Output lands in C:\LiteStepTNG\bin\Release_x64\
```
- A change in **`ModuleKit`** (static lib) requires rebuilding **every** module that links
  it — build the whole solution, don't just build one module.
- Check for real errors with: `... 2>&1 | Select-String ': error|fatal error|error C'`.

### Deploy to the runtime
```powershell
Copy-Item "C:\LiteStepTNG\bin\Release_x64\<Module>.dll" "C:\LiteStepRun\<Module>.dll" -Force
```
Copy only the DLLs you changed (or all of them). Then restart the shell (see §3).

---

## 3. Runtime architecture (IMPORTANT — changed 2026-11-21)

**Explorer is the Windows shell; litestep runs on top of it in overlay mode.**
```
HKCU...WinlogonShell            (no value; the HKLM default explorer.exe applies)
HKCU...CurrentVersionRunLiteStep = "C:LiteStepRunlitestep.exe" -overlay
```
The earlier setup (litestep as the Winlogon shell, explorer not running) was abandoned
because too much of Windows depends on Explorer's immersive shell: UWP apps (Settings,
Calculator), `ms-settings:` links, media-key routing, native virtual desktops. See §8 for
what was tried. The old value is saved in `C:LiteStepRunwinlogon-shell-backup.txt`.

Consequences:
- **Restart litestep with:** `Stop-Process -Name litestep -Force`, then
  `Start-Process C:LiteStepRunlitestep.exe -ArgumentList '-overlay'`. Nothing respawns
  it any more. **Always pass `-overlay`**: without it litestep sees Explorer's taskbar and
  shows a blocking Yes/No/Cancel prompt.
- `litestep.exe !Bang args` (via `Start-Process ... -ArgumentList '!Bang args'`, so the
  command line starts with `!` unquoted) reaches the running instance in this mode and
  is the way to test bangs without touching the mouse.
- **Explorer's taskbar is hidden by nCore** (`HideExplorerTaskbar true` in theme.rc): a
  1 s timer re-hides `Shell_TrayWnd` / `Shell_SecondaryTrayWnd`, and they are shown again
  when Core unloads. If litestep is killed or crashes the taskbar stays hidden until
  litestep (or Explorer) is restarted.
- **Work area:** `ReserveScreenTop` / `ReserveScreenBottom` (px, primary monitor). Core
  writes `SPI_SETWORKAREA` **without** `SPIF_SENDCHANGE` and re-checks every second.
  Broadcasting the change makes Explorer recompute its own value immediately (a fight
  litestep loses). AppBars are not usable: `SHAppBarMessage` is answered by litestep's own
  `Shell_TrayWnd` (tray service) and crashed in `shcore.dll`. `Desk.dll` is not loaded in
  this mode (its desktop would sit under Explorer's and it resets the work area on load).
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
# full-screen grab of the primary monitor (3440x1440 here)
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

## 4. The active theme: "Metamorphosis" (port of classic *LiteStep Enhanced*)

Goal of the ongoing work: port Norman Kaczmarek's 2002 "LiteStep Enhanced" (metamorphosis)
theme to this build, matching the reference image **`D:\Downloads\preview-6-223.jpg`**.

- Runtime theme files: **`C:\LiteStepRun\themes\Metamorphosis\`** (`theme.rc`, `images\`).
- Selected via `C:\LiteStepRun\theme-select.rc` → `ThemeName "Metamorphosis"`, included by
  `step.rc`. Other themes (Glass, Prototype-X) remain switchable the same way.
- Loader chain: `step.rc` → `theme-select.rc` (`ThemeName "X"`) → `themes\X\theme.rc`.

### Layout (matches the preview)
- **Top bar:** clock+date (`MClockTime`/`MClockDate`), virtual desktops, **MENU** button
  (`MMenuHit`), username label (`MUser`, purple, `$USERNAME$'S DESKTOP`), now-playing
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
- **Now-playing:** `Core/TextFunctions.cpp` adds a `[NowPlaying]` dynamic function backed by
  a background thread using **GSMTC** (`winrt Windows.Media.Control`), formatting
  `"Artist - Title"`. `Core/ModuleEntry.cpp` fires `DynamicTextChangeNotification(L"NowPlaying",0)`
  in `WM_TIMER`. `Core/Core.vcxproj` needed `LanguageStandard=stdcpp17`,
  `#pragma comment(lib,"windowsapp.lib")`, and `_ENFORCE_MATCHING_ALLOCATORS=0`.
- **Media control (Core):** `Core/TextFunctions.cpp` — the GSMTC thread also runs queued
  transport commands and tracks the play position. Bangs: `!MediaPlayPause`, `!MediaPlay`,
  `!MediaPause`, `!MediaStop`, `!MediaNext`, `!MediaPrev`, `!MediaSeek <0-100>`,
  `!MediaVolume <0-100 | +n | -n>` (master volume via `IAudioEndpointVolume`). Exports
  `MediaGetPermille(what)` / `MediaSetPermille(what, permille)` (0 = volume, 1 = track).
  Do **not** use `!KeyCombo MEDIA_*` for player buttons: simulated media keys are routed
  by explorer.exe and go nowhere when litestep is the shell.
- **Slider labels (Label):** `<Prefix>Slider Volume|Track` + `<Prefix>SliderHandle <label>`
  turn an nLabel into a slider; the handle is normally an overlay child with
  `ClickThrough true`. It polls Core every 500 ms, click/drag sets the value (track seek
  only on release). `Label/ModuleEntry.cpp` also registers the standard window bangs as
  `!nLabelMove|Show|Hide|SetText... <label> [args]`.
- **Virtual desktops (VWM):** `VWM/ModuleEntry.cpp` was an empty stub and did not export
  its entry points. It now parks the windows of other desktops 20000 px to the right and
  tags them with the window property `LSVWMDesk`; it gathers everything on load and on
  unload so a recycle never strands a window. Bangs `!VWMDesk <n>`, `!VWMNext`,
  `!VWMPrev`, `!VWMGather`; RC `VWMDesktops`, `VWMDesk<N>Command`. Activating a parked
  window (taskbar, Alt+Tab) switches to its desktop. Windows' own virtual desktops need
  explorer.exe and are not available.
- **Pinned apps popup:** `!PopupPinned` (content source `PINNED` in `Popup/ContentPopup.cpp`)
  lists the Windows taskbar pins: the `.lnk` files in
  `%APPDATA%\Microsoft\Internet Explorer\Quick Launch\User Pinned\TaskBar` plus packaged
  (Store) apps, which have no shortcut and are found by scanning the binary
  `HKCU\...\Explorer\Taskband\Favorites` for `PackageFamilyName!AppId` at both UTF-16
  alignments. Packaged apps are named/iconed via `shell:AppsFolder\<id>` and launched with
  `explorer.exe "shell:AppsFolder\<id>"`. Reloaded on every open (content popups are dynamic).
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
- **Aspect ratio differs from the preview.** The reference is 1024×768 (4:3); the dev machine
  is 3440×1440 (21:9). Element positions use `100%`-relative coordinates, so judge the match
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
  **Workaround in place (user's choice):** the theme's config menus use classic Win32
  panels instead of `ms-settings:` — `control.exe` + `sysdm.cpl`, `mmsys.cpl`, `ncpa.cpl`,
  `timedate.cpl`, `intl.cpl`, `appwiz.cpl`, `powercfg.cpl`, `main.cpl`, `firewall.cpl`,
  `/name Microsoft.NetworkAndSharingCenter`, `/name Microsoft.UserAccounts`, `sndvol.exe`,
  `rundll32 sysdm.cpl,EditEnvironmentVariables`, and the `explorer.exe shell:::{GUID}`
  folders for Devices and Printers / Desktop Background. All verified to open a real
  window on this machine. `desk.cpl` redirects to the Settings app and `bthprops.cpl`
  errors, so Display, Bluetooth and Windows Update have no working entry.
  `litestep.exe !Bang` from a command line does not reach the running instance here
  (its `FindWindow` for the main window fails), so it cannot be used to test bangs.
  Gotchas: litestep shows a blocking Yes/No/Cancel prompt at startup if an explorer
  `Shell_TrayWnd` exists; `explorer.exe ms-settings:` launchers linger as stuck processes.
- **Do not synthesize mouse input while the user is at the machine** (they are usually
  using it): a test right-click landed on their browser. Verify with `WindowFromPoint` that
  the target belongs to litestep, and prefer screenshots of what the user already has open.
- Click paths of the 2026-11-21 media/slider/VWM work (player buttons, slider drag, desktop
  switching) were built and load correctly but were only verified passively (knob follows
  the track and the volume); the user tests the clicks.
- `modules/Popup/ContentPopup.cpp` has a `TODO(human)` in `FormatThemeTitle` (how to mark
  the active theme in `!PopupThemes`); the lslnk popup title position inside the white
  header box (`MMMenuLnk*` in theme.rc) was not visually verified.

- Full side-by-side screen-vs-preview parity was still being tightened at last session end;
  the structural match is complete (all elements present, placed, and styled). Residual
  differences are PNG-skin micro-details, not layout or function.
- `modules/TaskBar/Taskbar.cpp` (the `lines` clamp) is the only code change ready to commit
  on its own merits; the rest are theme assets/config under `C:\LiteStepRun\themes\Metamorphosis\`
  plus the feature source above.
