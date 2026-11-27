# Theme installation: nModules first, and a plan a future downloader can read

## Why
Installing a theme today is copying a folder by hand. Nothing says what a theme needs (modules,
fonts, mode), nothing checks it, and a missing DLL just leaves the theme half loaded. The
classic way to get modules, `*NetLoadModule label-1.99`, is dead: `lsapi\SettingsFileParser.cpp`
only logs the line and the mirror it used no longer exists.

The user wants: when a theme is installed, **the nModules are used instead of the modules the
theme names**, and the design must leave room for a *later* module that downloads what a theme
still needs when it is installed or switched.

## Decisions taken with the user
- Approach **C (hybrid)**: a resolver in the core is the source of truth; an optional manifest
  declares a theme's requirements; installing or switching a theme produces a plan file listing
  what is resolved and what is missing; the future downloader only consumes that file.
- **nModules win.** If a name has an entry in the module map, the nModule is loaded even if the
  original DLL exists in the run folder.

## Goals / non-goals / anti-goals
- Goal: a theme that names classic modules (`label`, `popup2`, `vtray`, `ckvwm`...) loads the
  matching nModules with no edit to the theme.
- Goal: `install-theme` and `switch-theme` tell the user, before restarting, what is mapped,
  what is satisfied, what is missing and what is blocked.
- Goal: the plan file is a stable contract, so the downloader can be written without touching
  the resolver or the install tools.
- Non-goal: translating the *configuration* of classic modules (`*Label`, `*Popup` lines use keys
  the nModules do not read). Swapping the DLL does not make a 2002 theme look right; native
  themes are the target.
- Non-goal: the downloader itself, any network access, signature or hash checking (fields are
  reserved, see Manifest).
- Non-goal: making `original-2002\` or `drafts\` installable.
- Anti-goal: any change to how the five themes in `themes\` load today.
- Anti-goal: ever loading `Desk.dll` on top of Explorer because a theme or an alias asked for it
  (AGENTS.md §3).

## Approaches considered
- **A. Resolver at load time only** (alias table consulted by `ModuleManager`). Smallest, works
  for hand-copied themes, but no report before the shell restarts and nothing for a downloader
  to read.
- **B. Rewrite at install time** (script edits the theme's `LoadModule` lines). Modifies third
  party files, misses themes copied by hand, duplicates logic with `deploy.ps1`.
- **C. Hybrid. Chosen.** A is the source of truth; the install tools only analyse and report; the
  plan file is the seam for the downloader.

## Design

### 1. Module map and resolver (C++)
- `config\module-map.rc`, repo-owned and shipped like `config\menus`, lines of the form
  `*ModuleAlias <classic-name> <nModule>` (target without `.dll`). Included by `step.rc`
  **after `personal.rc`** (first definition wins), so a user can override an entry in
  `personal.rc`; an alias to itself (`*ModuleAlias popup2 popup2`) means "load the original".
- Known entries to start from (the builder completes and verifies the list against `modules\`):
  `label`, `lsslider` -> Label; `popup`, `popup2`, `lslnkmenu` -> Popup; `taskbar`, `taskbar3`
  -> TaskBar; `vtray` -> SysTray; `ckvwm` -> VWM; `shortcut`, `shortcut2`, `jdesk` -> Icon
  (**never Desk**). No entry for `geekamp`, `lsxcommand`, `textedit`: they are reported missing.
- Resolution happens in `litestep\ModuleManager.cpp` where the module name becomes a `Module`
  (`_MakeModule`, used by both `_LoadModules` and `LoadModule`). Name normalisation: take the
  file name, drop directory, `.dll` and a trailing `-<version>` (`label-1.99` -> `label`),
  compare case-insensitively. On a hit, load `$LiteStepDir$<nModule>.dll`. A name that already
  equals its target (`$LiteStepDir$Popup.dll`) is untouched, so today's themes behave exactly as
  now. If two lines resolve to the same DLL, the second is skipped and logged once.
- `*NetLoadModule` / `*NetInstallModule` lines become real module requests: they go through the
  same resolver (today they are only logged). If unresolved and the DLL is not present, the
  line is skipped and logged; **no download happens in this feature**.
  *(Builder-check call)* `ModuleManager::_LoadModules` reads them in a second loop **after** the
  `LoadModule` lines, so a theme's explicit `LoadModule` order (Core first) is never disturbed.
  *(Revised 2026-11-27)* One module is added that the theme does not ask for: when a line was
  mapped to an nModule and no queued module is `Core.dll`, `<run folder>\Core.dll` is loaded
  first, because every nModule needs nCore and a 2002 theme never loads it (each mapped module
  showed "problem connecting to nCore!"). The plan lists it as `Core`, note "loaded for the
  nModules". The goal "no edit to the theme" wins over "add nothing": Core first is also the
  load order the native themes use.
- *(Builder-check call)* **Fallback.** "nModules win" is a preference, not a ban: if the alias
  target DLL is not in the run folder but the original is, the original is loaded and the log
  and plan say so (`ok`, with `note: "original used, nModule not installed"`). Only when neither
  exists is the item `missing` (named after the target nModule).
- *(Builder-check call)* **Normalisation**, identical in C++ and PowerShell: file name only,
  lower-case, strip `.dll`, then strip a trailing `-` + text that starts with a digit
  (`label-1.99` -> `label`, `textedit-2.4l-13` -> `textedit`). `taskbar3` / `popup2` have no
  dash and are kept as written, so the map lists both spellings. A shared case list lives in
  `tools\tests\fixtures\module-names.txt`; a unittest feeds it to the PowerShell function
  (skipped when `pwsh`/`powershell` is absent) and the C++ side is checked from the log of a
  running shell.
- *(Builder-check call)* **Reading the map.** Lines are `*ModuleAlias <name> <target>` read in
  order (the resolver iterates them like `LoadModule`); the first line for a name wins, so
  `personal.rc`, included before the map, overrides it.
- *(Builder-check call)* **Desk.** No alias targets it, and the plan marks it `blocked` for an
  `overlay` install. *(Revised 2026-11-27)* The runtime refuses it too, on any line, while
  Explorer's desktop view (a `SHELLDLL_DefView`) exists: the shell-mode themes that needed it
  and `Desk.dll` itself were removed on 2026-11-26, and a theme that brings its own copy would
  take Explorer's desktop away (AGENTS.md §3). In a session where LiteStep is the shell there is
  no such view, so it is not refused there.
- Every substitution and every skip is logged (`Logger::Log`), including the theme line number
  when available, so the plan and the log agree.

### 2. Manifest `themes\<Name>\theme.json` (optional)
```json
{
  "name": "LiteStep Enhanced",
  "version": "1.0",
  "author": "...",
  "mode": "overlay",
  "requires": {
    "modules": [ { "name": "Notify", "url": null, "sha256": null } ],
    "fonts":   [ { "name": "hooge 05_55", "url": null, "note": "..." } ]
  }
}
```
- `mode` is `overlay` (default) or `shell` (the theme loads `Desk.dll`; not installable on top
  of Explorer, reported `blocked`). It replaces the hard-coded list in `package.ps1` as a later
  step (phase 4).
- `url` and `sha256` are **reserved for the downloader**: the install tools read and copy them
  into the plan but never fetch.
- Without a manifest, requirements are derived by scanning the theme's `LoadModule` /
  `*NetLoadModule` lines (following `include`s, expanding `$ThemeName$` and `$LiteStepDir$`).
  With one, the manifest is authoritative and the scan only adds modules the manifest forgot
  (reported as a warning).

### 3. Plan file `<run folder>\plans\<Theme>.json`
One file per theme (*builder-check call*: a single shared file would keep only the last theme
deployed), rewritten on every install, switch or deploy of that theme. Item `name` is the name
as the theme wrote it. Timestamps are UTC ISO 8601. Stable contract:
```json
{
  "theme": "LiteStep Enhanced",
  "generated": "2026-11-26T12:00:00Z",
  "items": [
    { "kind": "module", "name": "label-1.99", "status": "mapped",  "resolved": "Label.dll" },
    { "kind": "module", "name": "Notify",     "status": "ok",      "resolved": "Notify.dll" },
    { "kind": "module", "name": "geekamp-1.9.7", "status": "missing", "url": null, "sha256": null },
    { "kind": "module", "name": "Desk",       "status": "blocked", "reason": "takes over Explorer's desktop" },
    { "kind": "font",   "name": "hooge 05_55","status": "missing", "url": null }
  ]
}
```
Statuses: `ok` (DLL present), `mapped` (replaced by an nModule that is present), `missing`
(needed, not present, not mappable), `blocked` (must not load in this mode). `mapped` also
requires the target DLL to exist; if it does not, the status is `missing` with the target as
`name`. The downloader's contract: act only on `missing`, write into the run folder, then ask
the shell to recycle; it does not need to know about aliases.

### 4. Tools (PowerShell, like the rest of `tools\`; end users have no Python)
- `tools\theme-plan.ps1`, dot-sourced: `Get-ThemePlan -ThemeDir -LiteStepDir` implements
  sections 2 and 3, including the installed-fonts check, and **reads `config\module-map.rc`** so
  the C++ resolver and the tool cannot disagree.
- `tools\install-theme.ps1 <folder|zip> [-Force] [-LiteStepDir]`: validates (a `theme.rc` at the
  root of the theme folder, no entry in a zip escaping the target, name not already installed
  unless `-Force`), copies to `themes\<Name>`, writes the plan, prints a report. It does not
  select the theme and does not restart. *(Builder-check calls)* `<Name>` is the folder name,
  because `step.rc` builds paths from it; a zip with a single top-level folder is unwrapped, a
  zip with `theme.rc` at its root takes the manifest `name`, else the zip's file name; a
  manifest `name` that differs from the folder is a warning. A case-insensitive clash counts as
  already installed.
- `tools\switch-theme.ps1`: before rewriting `theme-select.rc` it builds the plan and prints
  it; with `missing` items it still switches (the shell works without them) but says so. **Order
  of a switch, which the downloader will hook into: plan -> (future: download) -> write
  `theme-select.rc` -> restart.**
- `deploy.ps1` keeps its job (repo themes to the run folder) and also refreshes the plan for the
  installed themes.

### 5. Order of work and checks
1. Resolver + `module-map.rc` (C++). Check: `tools\rc_effective.py` dumps of all five themes
   equal before and after (they do not change); a scratch theme with `*NetLoadModule label-1.99`
   loads `Label.dll` (log line) in the running shell.
2. `theme-plan.ps1` + manifest for the five themes. Check: plans of the five themes have no
   `missing` except fonts that are genuinely absent.
3. `install-theme.ps1`, `switch-theme.ps1`. Check: install a fixture theme folder and a zip;
   switching prints the plan.
4. Docs (AGENTS.md §2/§5), and `package.ps1` reading `mode` from manifests instead of its list.

Tests: `tools\tests\test_module_map.py` (same style as `test_config_menus.py`): every alias
target has a project under `modules\`, none targets `Desk`, no duplicate source names, and a
fixture theme under `tools\tests\fixtures\` for the plan format. The PowerShell tools are checked
by running them on the fixtures; the C++ resolver by the log of a running shell (no C++ test
harness exists in this repo).

## Fonts (builder-check call)
Manifests list only fonts that Windows does not ship (`hooge 05_55` for LiteStep Enhanced;
Segoe UI and other stock fonts are assumed present and not listed). Installed means present in
the system fonts or the per-user fonts (`HKLM`/`HKCU` `...\Fonts` registry keys, plus
`%LOCALAPPDATA%\Microsoft\Windows\Fonts`). The builder reads the font keys of the other four
themes and lists only non-stock ones.

## Left to the builder
- Final alias list and its exact normalisation of version suffixes (`taskbar3-0.305`,
  `textedit-2.4l-13`).
- JSON parsing in PowerShell 5.1 (`ConvertFrom-Json` is enough) and how the fixtures are run.
- Whether `*ModuleAlias` lines are parsed by `ModuleManager` directly or through a small
  helper in `lsapi`.

## Open follow-ups (not part of this work)
- The downloader module (fetch, verify `sha256`, extract, recycle).
- ~~The installer's hard-coded font note could be generated from the manifest.~~ Done
  2026-11-27: `package.ps1` writes it from the shipped themes' `requires.fonts`.
- Translating classic module configuration, if it ever becomes a goal.
