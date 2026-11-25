# Multi-language Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every theme text follows the Windows display language (en default; es pt fr de it zh ja), clocks name months/days in it, and LiteStep Enhanced switches to a CJK font for zh/ja.

**Architecture:** lsapi defines `SystemLanguage` before step.rc is read; step.rc sets `Language $SystemLanguage$` after personal.rc (first wins) and includes `lang\$Language$.rc` then `lang\en.rc`; themes include their own `lang\` the same way. Texts are `Txt*` variables; Core's `[Time]` uses the language's C runtime locale.

**Tech Stack:** C++ (lsapi, Core), `.rc`, Python tests (`tools/tests`), `tools/rc_effective.py`.

**Spec:** `docs/superpowers/specs/2026-11-25-multilanguage-design.md`

## Global Constraints
- Supported codes: `en es pt fr de it zh ja`; anything else → `en`. `zh-*` → `zh` (Simplified).
- Every `Txt*` value quoted (a `$Var$` expands to its first token, unquoted).
- Product names (Word, Excel, Firefox, Spotify, Edge, Chrome, Paint, Outlook, OneNote, PowerPoint, WordPad), `CPU/GPU/RAM/VRAM`, `cpu`/`mem` stay untranslated.
- Each theme keeps its date formats; only `%a %A %b %B` follow the language.
- `MUserText` empty in LiteStep Enhanced (user's decision).
- zh/ja fonts: LiteStep Enhanced only (pixel font at size 8, Aliased); others rely on DirectWrite fallback.
- Files UTF-8 without BOM; tests `python -m unittest discover -s tools/tests`; build with BuildTools MSBuild; never synthesize mouse input.

## Review Focus
- A `Txt*` key missing in a language → English shows (never empty). → Task 2 test `test_every_language_has_every_key`.
- personal.rc `Language` must win over the detected one. → Task 1 checker test + runtime check.
- Detection on a machine with an unusual UI language (e.g. `nl-NL`, `zh-TW`) → `en` / `zh`. → Task 1 unit-level mapping table in code, verified by forcing via personal.rc.
- `[Time]` with a locale the CRT does not know → today's output, no crash. → Task 4 fallback.
- CJK text wider than the rows in LiteStep Enhanced → readable, not clipped badly. → Task 5 screenshots with `Language ja` / `zh`.

---

### Task 1: Language detection
**Files:** `lsapi/lsapiInit.cpp`, `step.rc`, `tools/rc_effective.py` (builtin `SystemLanguage`, option `--system-language`), `tools/tests/test_rc_effective.py`.
- [ ] Test: tree with step.rc lines `Language $SystemLanguage$` after a personal file defining `Language ja` → `resolve('Language') == 'ja'`; without it → system value.
- [ ] lsapiInit: `GetUserPreferredUILanguages(MUI_LANGUAGE_NAME)` first name → primary subtag lower → map to supported else `en`; `SetVariable(L"SystemLanguage", code)`.
- [ ] step.rc after personal include: `Language $SystemLanguage$`, `include "$LiteStepDir$lang\$Language$.rc"`, `include "$LiteStepDir$lang\en.rc"`.
- [ ] Build, commit.

### Task 2: Shared texts
**Files:** `lang/{en,es,pt,fr,de,it,zh,ja}.rc`, `config/menus/*.rc` (texts → `$Txt…$`), `tools/tests/test_lang.py`, deploy/package copy `lang\`.
- [ ] Test `test_every_language_has_every_key` (same key set as en, per folder: `lang\` and each `themes\*\lang\`), `test_values_quoted`.
- [ ] Write the files; config menus use the variables. Deploy (`/MIR` like config) and package.
- [ ] rc_effective: `Language es` dump of each theme's config-menu lines equals today's texts; commit.

### Task 3: Theme texts
**Files:** `themes/*/menus.rc` and labels (texts → `$Txt…$`), `themes/LiteStep Enhanced/lang/*.rc` (categories, FECHA/HORA, theme-only words, zh/ja fonts), theme.rc includes `lang\$Language$.rc` + `lang\en.rc` first; `MUserText ""`.
- [ ] Theme-local keys only where not shared. zh/ja: `MMFont`/`MMFontSmall` overridden to the CJK font? NO — clocks keep the pixel font: override the style keys (`MMPixelTextFont`, popup item fonts `MMMenu*Font`, sizes, `TextAntiAliasMode Default`) in `lang\zh.rc`/`ja.rc`.
- [ ] rc_effective with `Language es` vs before: only listed texts; commit per theme.

### Task 4: Clock names (Core)
**Files:** `modules/Core/TextFunctions.cpp`.
- [ ] On load/refresh read `Language`, map to locale name (`en-US es-ES pt-BR fr-FR de-DE it-IT zh-CN ja-JP`), `_create_locale(LC_TIME, name)`; `Time` uses `_wcsftime_l` when the locale exists, else `wcsftime`.
- [ ] Build; runtime: `Language de` shows "NOVEMBER", `es` "NOVIEMBRE", `ja` "11月". Commit.

### Task 5: Verify and document
- [ ] personal.rc `Language ja`, then `zh`, then `en`, then removed: screenshots of bars and both menus in LiteStep Enhanced; smoke test.
- [ ] AGENTS.md: languages, `Txt*` keys, adding a language/text, the tests. Commit.
