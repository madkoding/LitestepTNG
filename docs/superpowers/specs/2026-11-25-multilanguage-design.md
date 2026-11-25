# Multi-language themes (session language, English by default)

## Why
Every user-visible text of the themes is hard-coded Spanish, and clocks print English month
names (`wcsftime` without a locale). The user wants the popups, all texts and the clocks in the
language of the Windows session, limited to the main languages, English when the session's
language is not one of them.

## Decisions already taken with the user
- Languages: **en (default), es, pt, fr, de, it, zh, ja**. Chinese = Simplified for every `zh-*`.
- Source: the Windows **display (UI) language** (`GetUserPreferredUILanguages`, first entry,
  primary subtag). The regional format is ignored. Detected at start-up (it only changes at
  sign-in). Overridable with `Language <code>` in `personal.rc`.
- Translated: menu entries, loading text, favourites texts, labels such as `FECHA:`, clock
  month/day names (each theme keeps its date format; see below). Not translated: what Windows supplies
  (folder and app names) and `CPU`/`GPU`/`RAM`/`VRAM`.
- A missing translation falls back to English, never to an empty text.
- zh/ja: each language file may override fonts and sizes. Latin languages keep `hooge 05_55`
  size 8 in LiteStep Enhanced; zh/ja use `Microsoft YaHei UI` / `Yu Gothic UI` at a size that
  fits the 15–18 px rows, antialiased. The clock time (digits) stays in the pixel font.
- Approach A: `$Language$` + shared `lang\<code>.rc` (texts common to all themes) + per-theme
  `themes\<Name>\lang\<code>.rc` (theme-only texts and fonts), English included after each to
  fill gaps (first definition wins). Rejected: everything per theme (≈1600 strings to keep),
  a Core translation function (popup titles are not reliably dynamic; language only changes at
  sign-in).

## Goals / non-goals / anti-goals
- Goal: switching `Language` changes every theme text, the clock names and, for zh/ja, fonts.
- Non-goal: switching language without a recycle; right-to-left languages; translating
  `original-2002\` or `drafts\`.
- Anti-goal: an English or Spanish session seeing any change other than texts (layout, sizes,
  colours stay; checked with `tools\rc_effective.py`: for `Language es` the effective settings
  equal today's except unified texts listed for the user).

## Design

### Detection (C++)
- `lsapi\lsapiInit.cpp`, next to the other built-ins: `SystemLanguage` = primary subtag of the
  first preferred UI language, lower-case, mapped to the supported set, else `en`.
- `step.rc`, after `personal.rc`: `Language $SystemLanguage$` (a `Language` in personal.rc
  comes first and wins), then `include "$LiteStepDir$lang\$Language$.rc"` and
  `include "$LiteStepDir$lang\en.rc"`.

### Texts
- `lang\<code>.rc`: `Txt<Name> "<text>"` variables for the shared menus (config\menus uses
  `"$TxtExplorer$"` etc.), the favourites pin texts and the loading text.
- Themes replace literal texts by `$Txt…$`; theme-only texts (LiteStep Enhanced: `FAVORITOS`,
  `JUEGOS`, `CARGANDO`, `FECHA:`/`HORA:`, LSLNK category titles…) live in
  `themes\<Name>\lang\<code>.rc`, included by the theme's `theme.rc` before its area files,
  English after.
- Values are quoted (a `$Var$` expands to its first token, unquoted).

### Fonts (zh/ja)
- The theme's `lang\zh.rc` / `lang\ja.rc` define the style fonts first (e.g. `MMPixelTextFont`,
  `MMPixelTextFontSize`, `…TextAntiAliasMode`), which win over the theme's own values because
  they are read first. Labels that set their font directly are moved to a style where needed.

### Clocks (C++, Core)
- `[Time('fmt')]` formats with the C runtime locale of `$Language$` (`_create_locale(LC_TIME,
  "<bcp47>")` + `_wcsftime_l`), created once at load and on refresh, so `%B`/`%A`/`%b` follow the
  language. Unknown or failing locale → current behaviour.

### Testing
- `tools\tests`: every key of `lang\en.rc` exists in every other `lang\*.rc` (and per theme);
  no file defines a key that en does not; values are quoted.
- `rc_effective` dumps per theme with `--personal` files setting `Language es|en|ja`: es equals
  today's settings except the listed texts; en/ja differ only in `Txt*`-derived lines and fonts.
- Runtime: `personal.rc` `Language ja` on the dev machine, screenshots of bars and menus, then
  removed; smoke test.

### Decisions after the builder check
User's answers:
- **LiteStep Enhanced's 2002 English words are translated where they are text** (option B):
  the LSLNK popup titles (OFFICE/GRAPHIC/MULTIMEDIA/INTERNET/TOOLS) follow the language even
  though the category button images keep their English words. Words that exist only inside
  PNGs (TRACK/VOLUME, the category buttons) are not changed.
- **The username label shows no text** (`MUserText` empty) in every language; its strip
  (`injektion3.png`) and its right-click menu stay. This is a deliberate visible change.

Implementer's calls:
- **Date formats stay each theme's own** (`%d. %B %Y`, `FECHA:  %d . %m . %Y`, Glass's
  `%A %d %B`…); only the names (`%B`, `%A`, `%b`, `%a`) follow the language through the Core
  locale. No `TxtDate*` keys: a "natural" per-language order would not fit the fixed clock
  boxes. Labels such as `FECHA:`/`HORA:` are translated.
- **Scope:** shared texts in all five themes. Per-language fonts only where the theme uses a
  font without CJK glyphs (LiteStep Enhanced's pixel font, and Prototype-X if its font lacks
  them); themes on Segoe UI rely on DirectWrite's font fallback. Shell-mode themes are checked
  with `rc_effective` only.

## Left to the builder
- Exact list of `Txt*` keys and their English names; the bcp47 tag per language for the C
  runtime locale; zh/ja font sizes per theme (must fit the rows: verify with screenshots).
- Translations are written by the builder; zh and ja should be reviewed by a native speaker
  later (noted in the final report).
