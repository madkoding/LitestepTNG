# RC Restructure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split every theme's `theme.rc` into small area files with `Group` styles, move shared menus/hotkeys/players to `config\`, and prove with a checker that each theme reads the same effective settings (except the listed menu unification).

**Architecture:** `tools/rc_effective.py` parses an rc tree like litestep (includes, first-definition-wins, star lines, `$Var$`, `Group` chains) and dumps effective values; baselines are taken before any theme is touched and every restructure is diffed against them. Themes then become `theme.rc` (ordered includes) + area files; `config\` holds shared look-free pieces deployed/packaged with the themes.

**Tech Stack:** Python 3 (stdlib only, `unittest`), LiteStep `.rc`, PowerShell deploy/package scripts.

**Spec:** `docs/superpowers/specs/2026-11-25-rc-restructure-design.md`

## Global Constraints
- No C++ changes. No new file format.
- Plain settings: first definition wins → a theme's own values are included before the shared defaults; `personal.rc` stays first (from `step.rc`). Star lines: include order = item order.
- One canonical version per shared entry: Windows settings via `ms-settings:` (classic command only where no page exists, each listed); everything else as LiteStep Enhanced (`explorer.exe`, `!MediaPrev`/`!MediaPlayPause`/`!MediaNext`, never `!KeyCombo MEDIA_*`).
- Shared menu texts in normal case with accents; icons via `.icon="$Icon<Entry>$"` defined per theme (empty = no icon; `ExtractIcon("")` returns NULL, verified in `PopupItem::ParseDotIcon`).
- `config\` is repo-owned: copied unconditionally by `deploy.ps1` and staged by `package.ps1` (the installer packs the whole stage).
- "Edit theme" menu entries open the theme folder: `!Execute [explorer.exe "$LiteStepDir$themes\$ThemeName$"]`.
- Theme files UTF-8 without BOM (AGENTS.md §5).
- Area files ~100–250 lines; `theme.rc` = header with the order rule + includes.
- Never synthesize mouse input; popups opened by bang only.

## Review Focus
- A label that had no value for a key now inherits one from a style (e.g. `TextAlign Center`): a silent visual change. → Task 1 checker enumerates group-reachable names (`test_new_inherited_key_is_reported`).
- Include order inverted so a shared default shadows a theme value. → Task 1 `test_first_definition_wins_across_includes`; caught by every theme diff.
- Star-line order changes inside a menu (an item moves). → Task 1 dumps star lines as ordered sequences (`test_star_line_order_matters`).
- `personal.rc` override lost after the split. → Task 4 diff run with a sample personal.rc (`--personal`).
- Variables referenced but no longer defined after a move (`$MMImg$` used in a file included before its definition is fine — expansion is at read time — but a dropped definition is not). → checker reports unresolved `$Var$` per tree; diff must not gain any (Task 1 `test_unresolved_variable_reported`).

---

### Task 1: Equivalence checker

**Files:** Create `tools/rc_effective.py`, `tools/tests/test_rc_effective.py`, `tools/tests/fixtures/` (tiny rc trees written by the tests into a temp dir are fine instead).

**Interfaces — Produces (CLI):**
```
python tools/rc_effective.py dump --root <dir> --theme <Name> [--personal <file>]   # stdout, sorted, stable
python tools/rc_effective.py diff <before.txt> <after.txt>                          # exit 0 if equal, 1 + listing otherwise
```
Dump format, one per line, sorted: `S <name-lowercase> = <expanded value>` for settings, `L <starkey-lowercase> <index> = <expanded line>` for star lines, `U <var>` for unresolved variables.

Python API used by tests: `load(root, theme, personal=None) -> Tree`; `Tree.resolve(name) -> str|None`; `Tree.dump() -> list[str]`.

Parsing rules (from `lsapi/SettingsFileParser.cpp`, `SettingsManager.cpp`): one setting per line, `;` starts a comment outside quotes, key = first token (case-insensitive), value = rest trimmed, surrounding quotes kept as litestep returns them for `GetRCLine` (strip one pair of outer quotes when comparing is NOT done — compare raw). `include "<path>"` expands `$Var$` in the path at parse time from settings already defined; missing file is skipped silently. Keys starting with `*` append to that star key's list. `if/elseif/else/endif`: raise `Unsupported` (no theme uses them today). Builtins: `LiteStepDir` = root with trailing `\`, `ThemeName` = `--theme`, `USERNAME` = `USERNAME` env. `--theme` defines `ThemeName` before `theme-select.rc` (first wins).

Resolution (from `modules/ModuleKit/Settings.cpp`): `resolve(N)`: if N defined → its value with `$Var$` expanded recursively (cycle-safe; unknown var → keep literal and record `U`). Else for i from len(N)-1 down to 1: if `N[:i]+"Group"` defined, `G` = its expanded value, r = `resolve(G + N[i:])` (cycle guard on the (prefix) chain); first non-None wins. Names dumped = every defined name ∪ every `P + S` where `P+"Group"` is defined and `G+S` is resolvable for any S reachable through G's own defined names and groups.

- [ ] **Step 1: tests** in `tools/tests/test_rc_effective.py` (each writes its rc files to a `tempfile.TemporaryDirectory` with a minimal `step.rc` including `themes\$ThemeName$\theme.rc`):
  - `test_first_definition_wins_across_includes`: theme.rc `include "$LiteStepDir$themes\T\a.rc"` then `include ...\b.rc`; a.rc `XColor red`, b.rc `XColor blue` → `resolve("XColor") == "red"`.
  - `test_keys_case_insensitive`: `xcolor red` → `resolve("XCOLOR") == "red"`.
  - `test_group_fallback`: `LabelGroup Style`, `StyleFontSize 8` → `resolve("LabelFontSize") == "8"`; `LabelFontSize 9` added before → `"9"`.
  - `test_group_child_suffix`: `MenuGroup Style`, `StyleItemFontColor #FF000000` → `resolve("MenuItemFontColor") == "#FF000000"`.
  - `test_group_chain_and_cycle`: `AGroup B`, `BGroup C`, `CSize 3` → `resolve("ASize")=="3"`; `CGroup A` added → no infinite loop, still `"3"`.
  - `test_variable_expansion_at_read`: `XImage "$Img$a.png"` before `Img C:\i\` defined → `resolve("XImage") == '"C:\\i\\a.png"'`.
  - `test_unresolved_variable_reported`: `XText "$Nope$"` → dump contains `U nope`.
  - `test_star_line_order_matters`: `*Popup "A" !a` then `*Popup "B" !b` vs swapped → `diff` exit 1.
  - `test_new_inherited_key_is_reported`: before `LabelX 1`; after adds `LabelGroup S`, `STextAlign Center` → dump(after) has `S labeltextalign = Center`, dump(before) does not → diff exit 1.
  - `test_if_unsupported`: a line `if 1` → raises `Unsupported`.
- [ ] **Step 2:** `python -m unittest discover -s tools/tests -v` → all FAIL (module missing).
- [ ] **Step 3:** Implement `tools/rc_effective.py` per the rules above.
- [ ] **Step 4:** same command → all PASS.
- [ ] **Step 5 (real-tree check):** `python tools/rc_effective.py dump --root . --theme Glass` contains `S npopupfont = ` with the value of `MenuStyleFont` (Glass groups `nPopupGroup MenuStyle`); compare by hand with the file.
- [ ] **Step 6:** Commit `tools: add rc_effective.py, an equivalence checker for rc trees`.

### Task 2: Baselines and shared `config\`

**Files:** Create `config/menus/apps.rc`, `config/menus/windows-settings.rc`, `config/menus/session.rc`, `config/hotkeys.rc`, `config/media-players.rc`; modify `tools/deploy.ps1`, `tools/package.ps1`.

**Interfaces — Produces:** file paths above, included as `include "$LiteStepDir$config\menus\apps.rc"`. Variables each theme must define for icons: `IconExplorer`, `IconTerminal`, `IconTaskManager`, `IconRun`, `IconPrograms`, `IconRecent`, `IconComputer`, `IconLock`, `IconRestart`, `IconShutdown` (extend as needed; listed in the header of each config file). Star key used inside the shared menu files: `*$MenuKey$` is NOT possible (star keys are not expanded) — so shared menu files are written for `*Popup`; a theme whose menu uses another star key (`*PopupStartMenu`) keeps a copy generated from the same text (Task 3 decides per case and records it).

- [ ] **Step 1 (baselines, before any change):** for each of the five themes, `python tools/rc_effective.py dump --root . --theme "<Name>" > .superpowers/rc-baseline/<Name>.txt` (git-ignored workspace). Also with `--personal personal.rc.example`.
- [ ] **Step 2:** Write the config files from LiteStep Enhanced's entries (normal case, accents), Windows settings mapped to `ms-settings:` pages; record the mapping table (entry → before command → after command) in the plan workspace for the final report.
- [ ] **Step 3:** `deploy.ps1`: `robocopy config <run>\config /MIR`; `package.ps1`: stage `config\`. Run `.\tools\package.ps1 -Version 0.0.0-test` → stage contains `config\menus\apps.rc`; delete the test output.
- [ ] **Step 4:** Commit `config: shared menus, hotkeys and media players for themes`.

### Task 3: LiteStep Enhanced

**Files:** `themes/LiteStep Enhanced/theme.rc` → includes; create `styles.rc`, `bar-top.rc`, `bar-bottom.rc`, `lslnk.rc`, `popups.rc` (skin), `menus.rc`, `notify.rc`, `frames.rc`, `icons.rc` (names may adapt to the existing sections; record).

- [ ] **Step 1:** Move sections verbatim into area files + includes (no other change). Checker diff vs baseline → empty. Commit `themes(LiteStep Enhanced): split theme.rc by area`.
- [ ] **Step 2:** Introduce styles (`MMPixelText`, `MMOverlayLabel`, …) and `Group` lines; delete the keys they make redundant. Checker diff → empty. Commit `themes(LiteStep Enhanced): fold repeated keys into styles`.
- [ ] **Step 3:** Replace its own menu entries with config includes + `Icon*` variables; "Edit theme" entries → open the folder. Checker diff → only the expected menu lines (texts with accents/case, ms-settings commands, edit entries). Save the diff for the final report. Commit `themes(LiteStep Enhanced): use the shared menus`.
- [ ] **Step 4:** Deploy (`.\tools\deploy.ps1 -ThemesOnly`), screenshots of top bar, bottom bar and `!PopupContext` (via the session's `popup-shot.ps1`) vs screenshots taken before Step 1; smoke test exit 0. Line counts before/after recorded.

### Task 4: Glass and Prototype-X
Same three steps as Task 3 per theme (split → styles → shared config), diff vs baseline each step, screenshots after switching to each theme with `tools\switch-theme.ps1` and back to LiteStep Enhanced at the end; Prototype-X media folder moves to `!Media*`. Also run the diff with `--personal personal.rc.example`. Commits per theme and step.

### Task 5: Windows11-Dark and Windows-nModules (checker only; shell-mode themes, never loaded here)
Same steps; `Windows-nModules` opts into config only where its star keys/bangs match (record). Commits per theme.

### Task 6: Documentation
AGENTS.md: a "Theme files" section in §5 (layout, order rule, styles via `Group`, `config\`, how to run the checker before/after any theme edit); update §1 repo layout (`config\`, `tools\rc_effective.py`), the "Edit theme" note. Commit `docs: describe the theme file layout and rc_effective`.
