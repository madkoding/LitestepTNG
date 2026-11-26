# Theme installation (nModules first + install plan) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A theme that names classic modules loads the matching nModules, and installing or switching a theme writes a per-theme plan file a future downloader can read.

**Architecture:** A pure C++ resolver (`litestep\ModuleResolver`) driven by `config\module-map.rc` is called from `ModuleManager`. PowerShell tools build a plan (`plans\<Theme>.json`) from the same map and an optional `theme.json`; the C++ and PowerShell name normalisers are tied together by one shared fixture.

**Tech Stack:** C++14 (MSVC; the resolver is std-only so g++ on Linux can test it), PowerShell 5.1, Python `unittest`.

**Spec:** `docs/superpowers/specs/2026-11-26-theme-install-design.md`

## Global Constraints
- nModules win over the modules a theme names; fall back to the original DLL only when the nModule DLL is absent (spec §1).
- No alias ever targets `Desk`; the runtime never refuses `Desk.dll` (spec §1, "Desk").
- `ModuleAlias` lines are `*ModuleAlias <name> <target>`, target without `.dll`, first line per name wins; `personal.rc` is included before the map.
- Normalisation: file name only, lower-case, strip `.dll`, then strip trailing `-` + text starting with a digit.
- The five themes in `themes\` must configure exactly what they do today (`tools\rc_effective.py` dumps equal).
- No network access anywhere in this work; `url`/`sha256` are copied, never used.
- Plan statuses: `ok`, `mapped`, `missing`, `blocked`. Plan path: `<run folder>\plans\<Theme>.json`, UTC ISO 8601 timestamps.
- Theme files are UTF-8 without BOM. Commits are semantic (`feat:`, `test:`, `docs:`, `build:`), each ending with the attribution trailers the session was given.
- This repo builds on Windows (MSBuild); PowerShell tools cannot run on Linux. Steps marked **[Windows]** are run on the user's machine; everything else runs here.

## Review Focus
- A theme with both `LoadModule $LiteStepDir$Popup.dll` and `*NetLoadModule popup2-2.0` loads Popup once (Task 2 test `test_duplicate_target_skipped`).
- Alias target absent, original present: the original loads and the plan says `ok` + note, not `missing` (Task 2 `test_falls_back_to_original`; Task 4 plan case).
- `*NetLoadModule` for an unknown name with no DLL is skipped without aborting start-up (Task 2 `test_unknown_bare_name_is_missing`).
- A zip entry escaping the target (`..\evil.dll`) is refused (Task 5 fixture `zip-traversal`).
- Malformed `theme.json` or a theme name with spaces/other case never crashes the tools: warning plus scan fallback (Task 4 fixtures `bad-manifest`, case-insensitive clash in Task 5).

---

### Task 1: Module map and its consistency test

**Files:**
- Create: `config/module-map.rc`, `tools/tests/fixtures/module-names.txt`, `tools/tests/test_module_map.py`

**Interfaces:**
- Produces: map lines `*ModuleAlias <name> <Target>`; fixture lines `<input> -> <normalised>` (blank lines and `;` comments allowed) used by Tasks 2 and 4.

- [ ] **Step 1: Write `tools/tests/test_module_map.py`** (style of `test_config_menus.py`). Tests: `test_every_line_is_a_moduleAlias_line`; `test_targets_have_a_module_project` (each target has a folder `modules/<Target>` or is `Core`); `test_no_alias_targets_desk`; `test_no_duplicate_sources` (case-insensitive); `test_sources_are_normalised` (each source equals itself under the normalisation rule); `test_fixture_is_well_formed`.
- [ ] **Step 2:** `python -m unittest tools.tests.test_module_map` from the repo root → FAIL (file missing).
- [ ] **Step 3: Create `config/module-map.rc`** with a header comment (what it is, first-wins, `*ModuleAlias popup2 popup2` keeps the original) and: `label`,`lsslider`→Label; `popup`,`popup2`,`lslnkmenu`→Popup; `taskbar`,`taskbar3`→TaskBar; `vtray`→SysTray; `ckvwm`→VWM; `shortcut`,`shortcut2`,`jdesk`→Icon. Verify each target against `modules\` while writing; drop or fix any that does not exist.
- [ ] **Step 4: Create `module-names.txt`** with at least: `label-1.99 -> label`, `taskbar3-0.305 -> taskbar3`, `textedit-2.4l-13 -> textedit`, `$LiteStepDir$Popup.dll -> popup`, `C:\x\Label.dll -> label`, `popup2 -> popup2`, `VTRAY-1.06 -> vtray`, `geekamp-1.9.7 -> geekamp`.
- [ ] **Step 5:** Re-run the test → PASS.
- [ ] **Step 6: Commit** `feat: add module map from classic modules to nModules`.

### Task 2: Pure C++ resolver with a g++ test

**Files:**
- Create: `litestep/ModuleResolver.h`, `litestep/ModuleResolver.cpp`, `tools/tests/module_resolver_test.cpp`, `tools/tests/test_module_resolver.py`
- Modify: `litestep/litestep.vcxproj` (add the two files next to `ModuleManager.*`, lines ~128 and ~157)

**Interfaces:**
- Produces (namespace `ModuleResolver`, std-only, no Windows headers, ASCII-only case folding):
  - `std::wstring NormalizeName(const std::wstring& location);`
  - `struct Alias { std::wstring from, to; };` `bool ParseAliasLine(const std::wstring& line, Alias& out);` (accepts `*ModuleAlias a b`, key case-insensitive; `from` normalised, `to` kept as written)
  - `class AliasMap { void Add(const Alias&); const std::wstring* Find(const std::wstring& normalizedName) const; };` first `Add` for a name wins.
  - `enum class Outcome { Unchanged, Mapped, KeptOriginal, Missing };`
  - `struct Resolution { std::wstring location; Outcome outcome; };`
  - `Resolution Resolve(const std::wstring& location, const AliasMap& map, const std::wstring& appPath, const std::function<bool(const std::wstring&)>& exists);` — appPath ends with `\`. A bare name (no directory, no extension) means `<appPath><name>.dll`.

- [ ] **Step 1: Write `module_resolver_test.cpp`** (plain `main`, returns non-zero on failure, reads `module-names.txt` path from `argv[1]`). Tests by name: `test_normalize_fixture` (every fixture line); `test_parse_alias_line` (valid, wrong key → false, missing target → false); `test_first_alias_wins`; `test_maps_to_nmodule` (`label-1.99` → `<app>Label.dll`, `Mapped`, when `Label.dll` exists); `test_falls_back_to_original` (target absent, `<app>label-1.99.dll` present → original path, `KeptOriginal`); `test_unknown_bare_name_is_missing` (`geekamp-1.9.7`, nothing exists → `Missing`); `test_self_alias_keeps_original` (`*ModuleAlias popup2 popup2`); `test_already_nmodule_unchanged` (`$LiteStepDir$Popup.dll`, expanded path ending `Popup.dll` → `Unchanged`, location untouched); `test_duplicate_target_skipped` is covered in Task 3 by the manager, not here.
- [ ] **Step 2: Write `test_module_resolver.py`**: skip when `g++` is absent; otherwise `g++ -std=c++14 -Ilitestep litestep/ModuleResolver.cpp tools/tests/module_resolver_test.cpp -o <tmp>` then run it with the fixture path; assert exit code 0 and show its output on failure.
- [ ] **Step 3:** Run `python -m unittest tools.tests.test_module_resolver` → FAIL (does not compile).
- [ ] **Step 4: Implement** the interfaces. Resolution order in `Resolve`: normalise; no alias → `Unchanged`; alias target equal to the normalised name → `Unchanged`; `<app><to>.dll` exists → `Mapped`; else original exists → `KeptOriginal`; else `Missing`.
- [ ] **Step 5:** Re-run → PASS. Add both new source files to `litestep.vcxproj`.
- [ ] **Step 6: Commit** `feat(litestep): add module name resolver`.

### Task 3: Wire the resolver into the shell

**Files:**
- Modify: `litestep/ModuleManager.h`, `litestep/ModuleManager.cpp` (`_LoadModules`, `_MakeModule`, `LoadModule`), `step.rc`, `tools/smoke-test.ps1`
- Create: `tools/tests/fixtures/theme-classic-demo/` (a one-file `theme.rc` that needs only the map, with `*NetLoadModule label-1.99`)

**Interfaces:**
- Consumes: everything from Task 2; `config/module-map.rc` from Task 1.
- Produces: member `ModuleResolver::AliasMap m_aliases` filled from `*ModuleAlias` lines (read in order with `LCReadNextConfigW(f, L"*ModuleAlias", ...)`) at the start of `_LoadModules`; `Module* _MakeModule(LPCWSTR, DWORD)` now resolves first and returns `nullptr` for `Outcome::Missing`; `step.rc` includes `config\module-map.rc` right after `personal.rc`.

- [ ] **Step 1:** In `_LoadModules`, after the `LoadModule` loop, add a second loop over `*NetLoadModule` then `*NetInstallModule` lines (same tokenising, first token is the key, second the name) feeding `_MakeModule`. Dedupe the combined queue by lower-cased resolved location, keeping the first and logging the skip with `Logger::Log`. Log every `Mapped`, `KeptOriginal`, `Missing`.
- [ ] **Step 2:** `LoadModule(pwzLocation, dwFlags)` (used by the `!LoadModule` bang and `ReloadModule`) goes through `_MakeModule`, so it resolves too.
- [ ] **Step 3:** Add the `include "$LiteStepDir$config\module-map.rc"` line to `step.rc` after `personal.rc`, with a comment saying why the order matters (first definition wins).
- [ ] **Step 4:** Add a check to `tools/smoke-test.ps1`: extend the existing "module loaded" loop so that, for the theme under test, any `*NetLoadModule` name that has a map entry is expected as the target DLL in `$litestep.Modules`.
- [ ] **Step 5: [Windows]** Build: `msbuild litestep.sln /p:Configuration=Release /p:Platform=x64 /m /verbosity:minimal /nologo`, no `error` lines. Run `python tools\rc_effective.py dump` for all five themes before (from `master`) and after; `diff` equal.
- [ ] **Step 6: [Windows]** Deploy, copy `tools/tests/fixtures/theme-classic-demo` into the run folder's `themes\` as `classic-demo`, switch to it, confirm `Label.dll` is in `litestep.exe`'s modules and the shell is still alive; switch back to `LiteStep Enhanced` and run `tools\smoke-test.ps1` (exit code 0).
- [ ] **Step 7: Commit** `feat(litestep): load nModules in place of classic modules`.

### Task 4: Plan generator and manifests

**Files:**
- Create: `tools/theme-plan.ps1`, `themes/<each of the five>/theme.json`, `tools/tests/test_theme_plan.py`, `tools/tests/run-plan.ps1`, `tools/tests/fixtures/plan/` (one theme folder per case, each with an `expected.json`)

**Interfaces:**
- Consumes: `config\module-map.rc`, `tools/tests/fixtures/module-names.txt`.
- Produces (dot-sourced; PowerShell 5.1 compatible):
  - `Get-ModuleMap([string]$LiteStepDir) -> hashtable` (lower-case name → target)
  - `ConvertTo-ModuleKey([string]$Name) -> string`
  - `Get-ThemePlan([string]$ThemeDir, [string]$LiteStepDir) -> pscustomobject` with `theme`, `generated`, `items[]` (`kind`, `name`, `status`, plus `resolved`, `note`, `reason`, `url`, `sha256` where the spec shows them)
  - `Write-ThemePlan($Plan, [string]$LiteStepDir)` → `plans\<Theme>.json`, UTF-8 without BOM
  - `Show-ThemePlan($Plan)` → coloured console report, grouped by status

- [ ] **Step 1: Write fixtures** under `fixtures/plan/`: `classic-demo` (NetLoadModule label/popup2/geekamp), `native-ok` (manifest + all DLLs present), `dup-target` (`LoadModule $LiteStepDir$Popup.dll` + `*NetLoadModule popup2-2.0` → one `Popup` item), `shell-mode` (loads `Desk.dll`, manifest `mode: overlay` → `Desk` `blocked`), `bad-manifest` (invalid JSON → warning item/field `warnings`, scan fallback), `fallback-original` (target DLL absent, original present → `ok` + note), `font-missing`. Each `expected.json` is the full expected plan minus `generated`.
- [ ] **Step 2: Write `run-plan.ps1`** (args: theme folder, output path): builds a temp run folder (copy of repo `config\module-map.rc`, empty files for the DLLs named by a `present.txt` in the case folder), calls `Get-ThemePlan`, writes the JSON to the output path.
- [ ] **Step 3: Write `test_theme_plan.py`**: skip without `pwsh`/`powershell`; for each case run the script and compare to `expected.json` ignoring `generated`. Add pure-Python tests that always run: `test_expected_files_are_valid` (every `expected.json` parses, statuses in the allowed set, `mapped` items have `resolved`).
- [ ] **Step 4:** Run → expected files test passes, PowerShell cases skip or fail (no script yet).
- [ ] **Step 5: Implement `theme-plan.ps1`** per the spec: scan follows `include`, expands `$LiteStepDir$` and `$ThemeName$`, reads `LoadModule` and `*NetLoadModule` (quotes allowed); manifest authoritative, scan adds forgotten modules as warnings; status rules in spec §3; fonts looked up in `HKLM`/`HKCU` `...\Fonts` and `%LOCALAPPDATA%\Microsoft\Windows\Fonts`; `Desk` is `blocked` when `mode` is `overlay`.
- [ ] **Step 6: Write the five `theme.json` files** (`mode: overlay` for LiteStep Enhanced, Glass, Prototype-X; `shell` for Windows-nModules and Windows11-Dark; modules copied from each theme's `LoadModule` lines; `fonts` only for non-stock fonts, read from each theme's font keys).
- [ ] **Step 7: [Windows]** Run `python -m unittest discover -s tools\tests` → all pass; plans of the five themes show no `missing` except genuinely absent fonts.
- [ ] **Step 8: Commit** `feat(tools): generate a per-theme install plan`.

### Task 5: install-theme, switch-theme and deploy

**Files:**
- Create: `tools/install-theme.ps1`, `tools/tests/test_install_theme.py`, fixtures `tools/tests/fixtures/install/` (`folder-theme/`, `zip-wrapped.zip` built in the test, `zip-traversal`)
- Modify: `tools/switch-theme.ps1`, `tools/deploy.ps1`

**Interfaces:**
- Consumes: Task 4 functions.
- Produces: `install-theme.ps1 -Source <folder|zip> [-Force] [-LiteStepDir]`, exit code 0 on success, non-zero with a message on refusal. Rules: spec §4 (single top-level folder unwrapped; `theme.rc` at zip root → manifest `name`, else zip name; entries resolving outside the target refused before anything is written; case-insensitive name clash = installed unless `-Force`; manifest/folder name mismatch = warning). It writes the plan and prints it; it neither selects nor restarts.

- [ ] **Step 1: Write `test_install_theme.py`** (skip without PowerShell): installs a folder theme into a temp run folder (`litestep.exe` empty file) and asserts `themes\<Name>\theme.rc` and `plans\<Name>.json` exist; wrapped zip unwrapped; `zip-traversal` exits non-zero and writes nothing; second install without `-Force` fails, with `-Force` succeeds; `Glass` vs `glass` counts as installed.
- [ ] **Step 2:** Run → FAIL.
- [ ] **Step 3: Implement `install-theme.ps1`** (use `System.IO.Compression.ZipFile`; validate every entry's full path starts with the staging root before extracting).
- [ ] **Step 4: Modify `switch-theme.ps1`:** build, write and show the plan before rewriting `theme-select.rc`; missing items print a warning but do not stop the switch. Keep the existing list mode and `-NoRestart`.
- [ ] **Step 5: Modify `deploy.ps1`:** after copying themes, write a plan for each installed theme.
- [ ] **Step 6: [Windows]** Run the unittest suite, then `.\tools\install-theme.ps1` on a scratch copy of `themes\Glass` renamed, and `.\tools\switch-theme.ps1 Glass -NoRestart`; both print a plan.
- [ ] **Step 7: Commit** `feat(tools): install themes and plan on switch`.

### Task 6: Docs and packaging

**Files:**
- Modify: `AGENTS.md` (§2 deploy/install, §5 module map and manifest, §6 resolver), `README.md` (Install), `tools/package.ps1`

- [ ] **Step 1:** `package.ps1`: replace the hard-coded `$Themes` with the folders under `themes\` whose `theme.json` has `mode: overlay` and that are not `original-2002\`/`drafts\`; keep the check that the selected theme ships. **[Windows]** run it with `-NoInstaller` and confirm the staged themes equal today's three.
- [ ] **Step 2:** Document in `AGENTS.md`: module map and first-wins override, fallback rule, manifest fields, plan contract (statuses, path, order of a switch) and where the downloader plugs in; in `README.md` how to install a theme.
- [ ] **Step 3:** Run `python -m unittest discover -s tools\tests`.
- [ ] **Step 4: Commit** `docs: describe theme installation and the module map`; `build: read theme mode from manifests in package.ps1` for Step 1 if committed separately.
