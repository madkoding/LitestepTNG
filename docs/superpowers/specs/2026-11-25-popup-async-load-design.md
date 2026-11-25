# Content popups load off the UI thread, with a loading row

## Why
Opening a folder / recent-documents / programs popup enumerates the folder and extracts every
icon in `ContentPopup::PreShow()` on litestep's main thread (shared by every module). Nothing
paints meanwhile, and the low-level mouse hooks on that thread (nHotKey desktop clicks, the
popup's outside-click hook) make the whole system's mouse stall until the hook times out. The
user reads it as "it freezes processes". Goal: the mouse never stalls and the user sees that
the popup is working.

## Goals / non-goals / anti-goals
- Goal: enumeration and icon extraction run on a worker thread; the popup opens at once with a
  single loading row and fills in all at once when the worker is done (option A, user's pick).
- Goal: the loading row looks like the theme (popup item skin, pixel font) with a small spinning
  arc, animated by `FrameClock`.
- Non-goals: progressive filling (option B, rejected); pre-loading at startup; `!PopupThemes`
  (reads a few files, stays synchronous); change-notification updates of a single item (stay
  synchronous, they are one item).
- Anti-goals: a crash or stranded thread when the popup closes, is recycled or the module
  unloads mid-load; flashing spinners on folders that load instantly; any change to what
  items a popup lists, their order, or their icons.

## Approaches
- **1. Worker prepares plain data, UI thread creates items (chosen).** Popup items are nModules
  windows and must be created on the UI thread; the expensive part (shell enumeration,
  `IExtractIcon::Extract`) is not.
- 2. Pump messages during the synchronous load (`PeekMessage` between items): little code, but
  the spinner stutters on each slow icon and re-entrancy (another popup, a recycle) mid-load
  is a crash risk. Rejected.
- 3. Pre-load in the background at startup: does not cover arbitrary folders or the always
  changing Recent folder. Rejected.

## Design
- `LoadSingleItem` is split into **read** (name, parsing name/command, folder-or-command,
  icon location + extracted `HICON`) and **create** (merge into an existing folder item, build
  `FolderItem`/`CommandItem`, set the icon, `AddItem`). `LoadRecent`, `LoadApps`, `LoadPinned`,
  shell folders and `PATH` all produce the same plain entry list through the read half.
- `PreShow` for an unloaded dynamic content popup (every source except `THEMES`): add a
  `LoadingItem`, start a worker (its own COM STA) with a copy of what it needs (source, paths,
  settings values such as `MaxRecentItems`, `noIcons`), mark the popup `loading`.
- The worker posts the finished list to the popup's window with a generation number. On
  arrival (UI thread): if the generation still matches, remove the loading row, create the
  items (icon cache: reuse a cached icon by hash and destroy the extracted duplicate), sort as
  today, re-size the visible popup and keep its position rule (`expandLeft`, confine to work
  area). Otherwise destroy the list's `HICON`s and drop it.
- Close before arrival: `PostClose` bumps the generation unless the popup is `keepLoaded`
  (then the result is kept and the next open is instant). Unload/module quit: bump and wait
  for running workers (they check a cancel flag between items), like `FrameClock::Shutdown`.
- `LoadingItem`: one row with the popup's normal item skin, text from
  `<Popup>LoadingText` (default `Loading...`; the LiteStep Enhanced theme sets `CARGANDO`,
  uppercased by `TextUppercase` anyway), and a ~10 px arc in the item's font colour that
  rotates via `Window::RequestFrames`. Text and arc stay invisible for the first 150 ms, so a
  fast folder shows a blank row for a moment and then its items, with no spinner flash.
- Already loaded popups (`keepLoaded`, not stale) open synchronously as today.

## Left to the builder
- Exact struct for an entry and where the worker lives (`ContentPopup` or a small
  `ContentLoader.cpp`), the private message id (registered, outside `LM_`), and how the 150 ms
  delay is timed (a `StopWatch` checked on each frame of the spinner).
- Verification without the mouse: open by bang (`!PopupRecentDocuments`, a `!PopupFolder`
  on a big folder such as System32, `!PopupApps` whose first open is ~2.5 s) and, while it
  loads, measure that a `WH_MOUSE_LL`-independent probe stays responsive: `SendMessageTimeout`
  to the litestep main window returns in < 50 ms during the load (before: blocks for the whole
  load). Screenshot the loading row and the filled popup. Close mid-load and `!Recycle`
  mid-load; litestep must survive. Smoke test.
