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
                   capture, session, media), hotkeys.rc, media-players.rc
tools/             deploy.ps1, smoke-test.ps1, switch-theme.ps1 (common.ps1 finds the run folder),
                   rc_effective.py (what a theme configures; see §5), tests\ (python -m unittest)
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
`.\tools\package.ps1 -Version <v>` stages a build plus the themes (not `original-2002\` or
`drafts\`) into `dist\stage` and produces `dist\LiteStepTNG-<v>-x64.zip` and, with Inno Setup 6
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
`-overlay`, `-nostartup`, `-startup`, `-explorer` and `-closeexplorer` are still accepted
and ignored, so old `Run` values and shortcuts keep working. The tray service stays: it is
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
- It then runs the Run keys and Startup folders (`StartupRunner`, back from before the
  removal), once per session (Explorer's `SessionInfo\<id>\StartupHasBeenRun` marker),
  skipping any entry that is litestep itself — a second instance closes the first.
- nWallpaper paints the desktop (`DesktopBackground.cpp`, window class
  `LSDesktopBackground`) only when no window holds a `SHELLDLL_DefView`. ShellAppRuntime
  creates a `Progman` of its own, so "is there a Progman" is the wrong test. The window
  stays at the bottom and runs `DesktopRightClick` / `DesktopMiddleClick`; `OnDesktop`
  windows (ModuleKit) go right above it.
- `!Quit` (or `LR_EXPLORER`) stops the host and starts explorer.exe, which then becomes the
  shell. Sign-out and shut-down do not.
- Tested without signing out (close Explorer gracefully with message 0x5B4 to its
  `Shell_TrayWnd`, so Windows does not restart it; then start litestep): host started,
  background painted, Calculator shown, `!Quit` handed the session back. Not tested yet:
  a real sign-in, `ms-settings:` links, toasts, media keys, Explorer folder windows.

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

- Full side-by-side screen-vs-preview parity was still being tightened at last session end;
  the structural match is complete (all elements present, placed, and styled). Residual
  differences are PNG-skin micro-details, not layout or function.
- `modules/TaskBar/Taskbar.cpp` (the `lines` clamp) is the only code change ready to commit
  on its own merits; the rest are theme assets/config under `themes\LiteStep Enhanced\`
  plus the feature source above.
