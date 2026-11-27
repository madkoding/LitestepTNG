# LiteStep TNG

A modern x64 build of the classic **LiteStep** desktop for Windows 10 and 11, with
Direct2D rendering and the nModules suite. It runs **on top of Explorer**: Explorer stays
the Windows shell, so Store apps, Settings and everything else that depends on it keep
working, while LiteStep replaces what you see — bars, menus, task list, tray, virtual
desktops, notifications and desktop icons — with a theme.

![The LiteStep Enhanced theme](docs/images/desktop.png)

The default theme, **LiteStep Enhanced**, is a port of Norman Kaczmarek's 2002 theme of
the same name.

| | |
|---|---|
| Clock, system meters, virtual desktops and the menu button | ![Left side of the top bar](docs/images/bar-left.png) |
| Media player controls, now playing, track and volume sliders | ![Right side of the top bar](docs/images/bar-right.png) |
| Program categories | ![Category bar](docs/images/launcher.png) |
| Windows notifications as boxes on the desktop | ![Notification boxes](docs/images/notifications.png) |

## Install

Download the latest release from the [releases page](../../releases/latest):

- **`LiteStepTNG-<version>-x64-setup.exe`** — the installer. It installs for the current
  user (no administrator rights), into `%LOCALAPPDATA%\Programs\LiteStepTNG`, and can
  start LiteStep when you sign in. It does not change the Windows shell.
- **`LiteStepTNG-<version>-x64.zip`** — the same files, to unpack anywhere you can write
  to. Start it with `litestep.exe`.

Requirements: Windows 10 version 2004 or newer, or Windows 11, 64-bit.

The LiteStep Enhanced theme is designed for the pixel font **hooge 05_55**, which is not
included. Without it Windows substitutes another font and the text looks different.

To leave LiteStep, run `litestep.exe !Quit` (or uninstall it); Explorer's taskbar comes
back. If LiteStep is killed instead, the taskbar stays hidden until LiteStep or Explorer
is started again.

## What is in it

- **Themes:** LiteStep Enhanced (default), Glass, Prototype-X and Windows-nModules. Change
  the theme from the menu, or by editing `ThemeName` in `theme-select.rc`.
- **Languages:** English, Spanish, Portuguese, French, German, Italian, Chinese and
  Japanese, chosen from the Windows display language (`Language` in `personal.rc` forces
  one).
- **Bars and labels** (`nLabel`, `Clock`): clock, date, live text such as `[CPU]`,
  `[GPU]`, `[Memory]`, `[VRAM]` and `[NowPlaying]`, sliders for volume and track position.
- **Task list and tray** (`nTaskbar`, `nTray`). Explorer's own taskbar is hidden while
  LiteStep runs (`HideExplorerTaskbar false` in a theme keeps it).
  - **One taskbar per monitor:** with several monitors, each one gets its own copy of the
    taskbar, listing the windows on that monitor, and maximized windows stop at it
    (`<Taskbar>EveryMonitor`).
  - **Minimized windows as thumbnails:** a minimized window shows a live picture of its
    content over its own task button. The picture grows out of the button when the window
    is minimized and shrinks back into it when the window returns; click it to restore the
    window (`MinimizedShelfEnabled`).
  - **The tray grows with its icons.** In LiteStep Enhanced it also shrinks when one goes;
    the other themes keep a minimum size, since their tray sits in the bar next to the
    taskbar (`<Tray>MinWidth`).
- **Shadows and animations:** menus, notifications, bars and desktop icons cast soft
  shadows that follow their shape, cut corners included, and fade in and out with their
  windows' animations (`<Prefix>ShadowPreset`, `TextShadowColor`). Notifications slide in
  and out, and task buttons slide into place.
- **Popup menus** (`nPopup`, `StartMenu`): programs, folders, favourites, recent
  documents, themes.
- **Media control:** buttons that drive the player you choose (the rotation is configured
  in the theme) through the Windows media session.
- **Virtual desktops** (`VWM`): four desktops with a transition; right-click a desktop
  button, or press Win+Shift+1..4, to send the active window there.
- **Notifications** (`Notify`): Windows toasts shown as theme boxes, in every theme; click
  one to go to its program, or close it with the X.
- **Desktop icons** (`Icon`): the Desktop folder drawn in the theme's style, with
  thumbnails and drag and drop.
- **Window frames** (`Frames`, experimental): a themed frame around other programs'
  windows that have a standard title bar.
- **Wallpaper:** a theme may bring its own (`ThemeWallpaper`); otherwise the wallpaper you
  choose in Windows is used.

### Personal settings

Things that belong to you live next to `litestep.exe` and are never overwritten by an
update:

- `personal.rc` — settings that override the theme (see `personal.rc.example`).
- `theme-select.rc` — the selected theme.
- `favorites\` and `games\` — shortcuts shown in the menu.

### Install a theme

The scripts are in the repository's `tools\` folder (they are not part of the release
files). Point them at the folder LiteStep runs from with `-LiteStepDir`, or the
`LITESTEP_DIR` environment variable, or just have LiteStep running:

```powershell
.\tools\install-theme.ps1 C:\Downloads\SomeTheme.zip     # or a folder with a theme.rc
.\tools\switch-theme.ps1 "SomeTheme"                     # select it and restart LiteStep
```

`install-theme.ps1` copies the theme to `themes\<Name>` (add `-Force` to replace one that
is already installed) and prints its plan, also saved as `plans\<Name>.json`: which modules
and fonts the theme needs and whether each one is **ok**, **mapped** to an nModule,
**missing** or **blocked**. Nothing is downloaded; missing items are only reported. The
theme still loads without them, minus whatever needs them: LiteStep skips and logs a missing
module named the classic way (`*NetLoadModule`) or by an alias, and a 32-bit one (classic
modules are 32-bit, LiteStepTNG is 64-bit: reported **blocked**). A missing DLL named by a
`LoadModule` line still stops start-up with LiteStep's error box until it is closed.

Classic themes name modules such as `label-1.99` or `popup2`; LiteStep loads the matching
nModule instead (`config\module-map.rc`, which `personal.rc` can override), and loads
`Core.dll` first when the theme does not, since every nModule needs it. Only the module is
swapped: the classic theme's configuration is not translated, so an old theme will not look
as it did.

## Architecture

```mermaid
flowchart TB
    subgraph config["Configuration (run folder)"]
        step["step.rc"]
        select["theme-select.rc"]
        personal["personal.rc (optional)"]
        theme["themes/&lt;name&gt;/theme.rc"]
        step --> select
        step --> personal
        step --> theme
    end

    subgraph core["LiteStep core"]
        exe["litestep.exe<br/>module loader, bangs, tray service"]
        lsapi["lsapi.dll<br/>settings, bangs, messages"]
        exe --- lsapi
    end

    subgraph modules["nModules (one DLL each, all link ModuleKit)"]
        kit["ModuleKit<br/>windows, Direct2D drawing, settings"]
        ncore["Core<br/>live text, meters, media, work area"]
        bars["Label, Clock, TaskBar, SysTray"]
        menus["Popup, StartMenu"]
        desk["Icon, Wallpaper, Notify"]
        wins["VWM, Frames, TaskSwitcher, HotKey"]
        ncore --> kit
        bars --> kit
        menus --> kit
        desk --> kit
        wins --> kit
        bars -. "text, icons" .-> ncore
        menus -. "icons" .-> ncore
        desk -. "thumbnails" .-> ncore
    end

    subgraph windows["Windows"]
        explorer["Explorer (the shell)<br/>desktop, wallpaper, Store apps"]
        apis["Direct2D / DirectWrite, WinRT<br/>media sessions, notifications"]
    end

    config --> exe
    exe -- "loads the modules the theme lists" --> modules
    modules --> lsapi
    modules --> apis
    ncore -- "hides the taskbar, sets the work area" --> explorer
```

Explorer remains the shell and keeps drawing the desktop and the wallpaper. `litestep.exe`
reads the configuration, loads the modules the theme asks for, and each module draws its
own windows on top.

## Build from source

You need Visual Studio 2022 (or its Build Tools) with the v143 toolset and a Windows 10
SDK.

```powershell
git clone <repository-url>
cd LitestepTNG
msbuild litestep.sln /p:Configuration=Release /p:Platform=x64 /m
```

Binaries land in `bin\Release_x64\`. LiteStep runs from a separate folder (the "run
folder"), not from the repository; the scripts in `tools\` take care of that:

```powershell
.\tools\deploy.ps1 -LiteStepDir <run folder>      # copy the build and the themes, restart
.\tools\smoke-test.ps1 -LiteStepDir <run folder>  # drive the running shell and check it
.\tools\package.ps1 -Version 1.2.3                # zip + installer (Inno Setup 6) in dist\
```

By default LiteStep runs on top of Explorer, which stays the Windows shell. (`-overlay`, the
switch older versions needed, is still accepted and does nothing.)

On Windows 11 LiteStep can also be the shell itself, with Explorer not running:
`.\tools\set-shell.ps1 LiteStep` (from the next sign-in; `.\tools\set-shell.ps1 Explorer`
undoes it). It then starts Windows' own `ShellAppRuntime.exe`, which is what lets Store apps
and Settings show and which runs the programs that start with Windows, and paints the desktop
background itself. Quitting LiteStep gives the session back to Explorer. This mode is new.

Every push is built and packaged by GitHub Actions (`.github\workflows\build.yml`);
pushing a tag `v<version>` publishes a release.

`AGENTS.md` is the detailed guide to the code: architecture, how the modules work, theme
authoring, and the reasons behind the less obvious decisions.

## Repository layout

```
litestep\     the core executable (litestep.exe)
lsapi\        the LiteStep API (lsapi.dll, lsapi.h)
modules\      the nModules DLLs and their shared library, ModuleKit
themes\       one folder per theme, each with a theme.rc
tools\        deploy, smoke test, theme install and switch, and packaging scripts
installer\    the Inno Setup script
step.rc       the loader: theme-select.rc, the optional personal.rc, then the theme
```

## Notes for theme and module authors

- Themes are `theme.rc` files for the nModules suite. Use `$LiteStepDir$`, `$ThemeName$`
  and `$USERNAME$` instead of literal paths.
- A `theme.json` next to `theme.rc` declares what the theme needs (modules, fonts that
  Windows does not ship) and its mode; see `AGENTS.md` for the fields.
- The desktop (wallpaper, icons) is Explorer's. A module must never re-parent Explorer's
  desktop view: that is why the old `Desk.dll` was removed.
- Modules are built against `lsapi\lsapi.h` and link `ModuleKit`.
- The core also contains an experimental declarative engine ("Theme Engine V2",
  `litestep\themev2`, files `theme.lsx` / `theme.lsxstyle`). The themes shipped here do
  not use it.

## Credits and license

LiteStep TNG builds on the work of the LiteStep Development Team, the nModules project,
and the LiteStepTNG fork this repository comes from. The LiteStep Enhanced theme was
created by Norman Kaczmarek.

LiteStep is free software, released under the GNU General Public License, version 2 or
later. See [LICENSE](LICENSE).
