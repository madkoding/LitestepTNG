# Smaller, decoupled .rc files (themes and shared configuration)

## Why
Each theme is one `theme.rc` (LiteStep Enhanced: 1137 lines, 897 without comments; the others
400–485). The user finds them too big to maintain. Two causes, both measured:
- **Repetition inside a theme.** Every label repeats the same keys: in LiteStep Enhanced
  `Color` appears 70 times, `Width`/`Height`/`X`/`Y` ~40 each, `BrushType`/`Image`/
  `ImageScalingMode` 24, `Font`/`FontSize`/`TextVerticalAlign` 17–18. The engine already has
  inheritance (`<Prefix>Group <Style>`, `modules/ModuleKit/Settings.cpp`), used 7 times there.
- **Duplication across themes that has drifted.** Menus, hotkeys and media players are copied in
  every theme and no longer agree (Glass "Explorador de archivos" vs Prototype-X "Explorador";
  "Configuración" with and without accent).

This is the first of two projects: the next one (multi-language: en default, es, pt, fr, de, it,
zh, ja; texts, clock names, per-language fonts) builds on this structure. Styles make that cheap
(a language file overrides one style's `Font` instead of 18 lines).

## Goals
- Every theme reads the same effective settings as before (proven by a tool, see Verification),
  except the deliberate unification of drifted shared menus, which is listed for the user.
- Shared, look-independent configuration lives once in `config\` and each theme opts in.
- Themes are split by area into files of roughly 100–250 lines; `theme.rc` becomes a short,
  ordered list of includes with a header explaining the order rule.
- Repetition is folded into named styles with `Group`.

## Non-goals
- No translation yet (next project); texts stay as they are, but shared menu texts come from one
  file.
- No engine/C++ changes. No new file format or generator.
- No visual redesign.

## Anti-goals
- A theme that looks or behaves differently in any state (hover, open, pressed, notifications,
  frames, icons) without it being on the reviewed list of intended differences.
- Breaking `personal.rc` overrides (it is included before the theme; first definition wins).
- Leaving the shell-mode themes (`Windows-nModules`, `Windows11-Dark`) broken because they
  cannot be run in overlay mode here: they are covered by the tool, not by screenshots.

## Approaches
- **B. Restructure by hand, verified by an equivalence checker (chosen).**
- A. By hand, screenshots only: misses non-resting states. Rejected.
- C. Generate `.rc` from YAML/JSON: changes the maintenance model and adds a build step.
  Rejected; the user asked for simpler `.rc`, not another format.

## Design

### Layout
```
step.rc                      includes only (unchanged role)
config\                      shared by themes, no look; a theme includes what it wants
  menus\apps.rc              Explorer, Terminal, Task Manager, Run
  menus\windows-settings.rc  the Windows settings/control panel entries
  menus\session.rc           lock, restart, shut down
  hotkeys.rc                 common hotkeys
  media-players.rc           *MediaPlayer rotation
themes\<Name>\
  theme.rc                   header + ordered includes only
  <area>.rc                  e.g. styles.rc, bar-top.rc, bar-bottom.rc, popups.rc,
                             menus.rc, notify.rc, frames.rc, icons.rc
```
- `config\` is deployed and packaged next to `themes\` (`tools\deploy.ps1`,
  `tools\package.ps1`, `installer\LiteStepTNG.iss` staging); themes reference it as
  `$LiteStepDir$config\...`.
- Shared menu files hold only `*Popup`/`*PopupStartMenu` item lines, no look and no root
  (`!New`) line; a theme includes them at the place in its menu where they belong. Since the
  key name decides which popup a line goes to, a shared menu file is included once per popup
  that uses it only if both popups use the same star key; otherwise the file uses a variable
  or the theme keeps its own copy (builder decides per case and lists it).

### Order rule (written at the top of every `theme.rc`)
Plain settings: the first definition wins, so a theme's own values come before the shared
defaults it includes, and `personal.rc` (included by `step.rc` before the theme) overrides both.
Star lines (`*Popup`, `*HotKey`, …): file order is item order.

### Styles
- Named, theme-prefixed styles (LiteStep Enhanced: `MM…`, matching its existing variables),
  e.g. a pixel-text style (font, size, colour, antialias mode, alignment) and an overlay-label
  style (transparent brush, click-through). Labels keep only what differs (position, size,
  text, images) plus `<Prefix>Group <Style>`.
- `Group` chains and child prefixes propagate (`CreateChild`: if `P` groups `S`, `PItem` falls
  back to `SItem`); styles may be layered (`S2Group S1`).

### Equivalence checker (`tools\rc-effective.py`)
- Parses an rc tree like litestep: `include "<file>"` with `$Var$` expansion of the path,
  case-insensitive keys, first definition wins, star lines appended in order, `;` comments,
  quoted values; `if`/`elseif`/`else`/`endif` if present (fail loudly on anything it cannot
  evaluate rather than guess).
- Resolves what a module reads, modelled on `Settings.cpp`: a `Settings(prefix)` object with its
  `Group` chain (cycle-safe), `CreateChild(suffix)` propagating the suffix along the chain, and
  `$Var$` expansion at read time (`SettingsManager`'s `VarExpansionEx`).
- Output: for every key name defined anywhere in the before or after tree, the value a module
  reading it would get, plus every star-line sequence; `diff` mode prints differences between
  two trees. Inputs: `step.rc` plus the theme name (and an optional `personal.rc`).
- Validated before use: unit cases for first-wins, include order, group chains with children,
  cycles, variable expansion; and against Glass, whose `MenuStyle` group the modules already use
  (expected values written by hand from the current file).

### Verification per theme
1. Checker baseline of the untouched theme.
2. Restructure; checker diff must be empty except the listed intended differences (shared-menu
   unification).
3. For themes that run in overlay mode (LiteStep Enhanced, Glass, Prototype-X): screenshots of the
   bars and the main popup before/after, smoke test. Shell-mode themes: checker only.

### Decisions taken after the builder check
User's answers:
- **One canonical version per shared entry** (drifted copies are unified, in text and in what
  they run). Windows settings entries use **`ms-settings:` links** (this changes LiteStep
  Enhanced, which used classic `control.exe` panels when UWP apps did not open under litestep
  as shell; Explorer is the shell now). An entry with no `ms-settings:` page keeps its classic
  command (e.g. environment variables via `rundll32 sysdm.cpl,EditEnvironmentVariables`, the
  Control Panel itself); the builder lists each mapping.
- **Everything else as LiteStep Enhanced:** Explorer runs plain `explorer.exe`; media entries use
  `!MediaPrev` / `!MediaPlayPause` / `!MediaNext` (never `!KeyCombo MEDIA_*`).

Implementer's calls (listed for the user to check):
- **Texts:** shared menus use normal case with accents ("Configuración"); the pixel font
  `hooge 05_55` has every Latin accented glyph (checked: Á É Í Ó Ú Ñ Ü Ç À Â Ê Ô Ã Õ Ä Ö ß), and
  LiteStep Enhanced's `TextUppercase` shows them as "CONFIGURACIÓN". Its accent-less uppercase
  texts were a porting choice, not a font limit.
- **Icons are a theme's look:** shared menu lines carry `.icon="$Icon<Entry>$"`; each theme
  defines those variables (themes without icons define them empty — the builder verifies that
  an empty `.icon=""` is ignored by the popup parser, else uses another neutral form).
- **`config\` is owned by the repo**, like `themes\`: deployed and packaged unconditionally,
  overwritten on every deploy/upgrade. Personal additions keep going in `personal.rc` (star
  lines there add items).
- **"Edit theme" entries** (LiteStep Enhanced menus and TOOLS lnk menu, Glass, Prototype-X) open
  the theme's folder in Explorer instead of `theme.rc`, which becomes a list of includes.
- Shell-mode themes (`Windows-nModules`, `Windows11-Dark`) opt into `config\` only where their
  star keys and bangs match; `Windows-nModules` keeps its own English menus if they do not.

## Left to the builder
- Exact style names and how many layers; exact area split per theme (sizes ~100–250 lines).
- Which hotkeys/menu entries are truly common enough for `config\`; anything theme-specific stays.
- Whether the checker is Python (preferred; Python is on this machine) or PowerShell.
