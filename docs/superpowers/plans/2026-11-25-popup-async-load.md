# Popup Async Load Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Content popups enumerate folders and extract icons on a worker thread and show a themed loading row meanwhile, so litestep's UI thread (and the system mouse, through its low-level hooks) never stalls.

**Architecture:** The read half of today's loaders moves to `Popup/ContentLoader.{hpp,cpp}` and produces plain `ContentEntry` records (strings + an extracted `HICON`). `ContentPopup` runs it on a worker for unloaded dynamic popups, shows a `LoadingItem`, and builds the real items on the UI thread when a registered message brings the result. Watches (`SHChangeNotifyRegister`) are registered on the UI thread from absolute PIDLs the worker returns.

**Tech Stack:** C++/MSVC, Win32 shell (`IShellFolder`, `IExtractIconW`, `SHGetFileInfoW`), Direct2D (spinner), ModuleKit `FrameClock` / `Window::RequestFrames`.

**Spec:** `docs/superpowers/specs/2026-11-25-popup-async-load-design.md`

## Global Constraints
- Branch `feat/popup-async-load`, based on `feat/frame-clock` (needs `FrameClock`).
- Same items, order, icons and merge behaviour as today (FolderItem merge by name, sorting rules in `PreShow`).
- `THEMES` stays synchronous; single-item change notifications stay synchronous.
- Loading row text: `<Popup>LoadingText`, default `Loading...`; theme `LiteStep Enhanced` sets `MMMenuLoadingText CARGANDO`. Text and arc hidden for the first 150 ms.
- Worker threads pin their module (`GetModuleHandleExW(FROM_ADDRESS)` + `FreeLibraryAndExitThread`), like `FrameClock`.
- COM objects never cross threads: the worker has its own `CoInitializeEx(COINIT_APARTMENTTHREADED)`; only strings, `HICON`s and PIDLs cross.
- Registered messages only (`RegisterWindowMessageW`), never in the `LM_` range.
- No unit tests exist: gate = full solution build with no errors; behaviour = Task 4 runtime checks.
- Do not synthesize mouse input. Open popups by bang only, close them with `!PopupClose`-style bangs if they exist, else `WM_ACTIVATE(WA_INACTIVE)` posted to the popup window (the same path the outside-click hook uses).

## Review Focus
- Popup closed, destroyed (submenu `SuicidalContentPopup`, recycle) or module unloaded while its worker runs: the result must be freed (HICONs, PIDLs) and never touch a deleted `ContentPopup`. → Task 2: worker frees the result itself when `PostMessage` fails; arrival checks generation; destructor bumps generation.
- Popup re-opened while the previous load still runs: exactly one result applied, no duplicate items. → Task 2: generation check, `loading` flag prevents a second start.
- A watched folder changes while the load runs (create/delete notification before items exist): no crash, popup ends consistent. → Task 2: watches are registered only on arrival.
- Very large result (System32 ~4000 entries, Apps ~300 icons): items created in one pass on the UI thread; that pass must stay short (icons pre-extracted). → Task 4 measures the arrival pass.
- Icon cache ownership: a cached hash reuses the cached icon and destroys the worker's copy; an uncached one is stored once. No leak, no double destroy. → Task 1 (`PopupItem::SetIcon(HICON, Hash)`).

---

### Task 1: Split reading from item creation (still synchronous)

**Files:**
- Create: `modules/Popup/ContentLoader.hpp`, `modules/Popup/ContentLoader.cpp`; add to `Popup.vcxproj(.filters)`
- Modify: `modules/Popup/ContentPopup.{hpp,cpp}`, `modules/Popup/PopupItem.{hpp,cpp}`

**Interfaces — Produces:**
```cpp
struct ContentEntry {
  std::wstring name, command;   // command = SHGDN_FORPARSING name (folders) or ready-to-run command
  bool folder;                  // openable -> FolderItem, else CommandItem
  HICON icon;                   // owned until handed to an item; nullptr = none
  bool hasIconHash; IconCache::Hash iconHash;  // set when the icon came from IExtractIcon
};
struct ContentRequest {
  ContentPopup::ContentSource source; std::vector<std::wstring> paths;
  int maxRecentItems; bool noIcons;
};
struct ContentResult {
  std::vector<ContentEntry> entries;
  std::vector<PIDLIST_ABSOLUTE> watch;   // folders to watch (ILClone'd), PATH and shell folders only
  void Free();                           // DestroyIcon + ILFree everything left
};
namespace ContentLoader {
  void Read(const ContentRequest &request, ContentResult &result, const volatile LONG *cancel);
  bool ReadItem(IShellFolder *folder, PCUIDLIST_RELATIVE item, bool dontExpandFolders, bool noIcons, ContentEntry &entry);
}
// PopupItem:
void SetIcon(HICON icon, IconCache::Hash hash);  // cache hit: use cached, DestroyIcon(icon); miss: AddIcon + StoreIcon
// ContentPopup (private):
void CreateItems(ContentResult &result);         // merge/create items, set icons, register watches, AddItem; leaves result empty
```
- `ReadItem` = today's `LoadSingleItem` minus item creation: name, parsing name, openable test, quoted command for non-folders, icon via `GetIconLocation`/`Extract` (64 → 32 → 16 → `ExtractIconW` → shell32,1, same order) with `IconCache::ComputeHash` (make it `static`). `LoadApps` entries carry the `SHGetFileInfoW` icon with `hasIconHash=false` (CommandItem HICON ctor, then `DestroyIcon`, as today). `LoadRecent` counts entries instead of `items.size()`. `cancel` is checked between items.
- `CreateItems` does the merge loop (`CheckMerge`), the dynamic/non-dynamic `FolderItem` lambdas, `CommandItem`, icons, and binds each `watch` PIDL (`SHGetDesktopFolder` → `BindToObject`) to register it exactly as `LoadFromIDList` does today.
- `PreShow` (still sync here): `ContentLoader::Read` → `CreateItems` → existing sort. Change notification `SHCNE_CREATE/MKDIR`: `ReadItem` + one-entry `CreateItems` (no watch).
- `THEMES` keeps `LoadThemes()`.

- [ ] **Step 1:** Implement. **Step 2:** build solution (`...\BuildTools\MSBuild\Current\Bin\MSBuild.exe litestep.sln /p:Configuration=Release /p:Platform=x64 /m /verbosity:minimal /nologo`) → exit 0, no `error`.
- [ ] **Step 3:** Deploy (`.\tools\deploy.ps1 -Modules Popup,StartMenu`, both link the item classes — check StartMenu builds them from `..\Popup\`), open `!PopupRecentDocuments` by bang, screenshot, compare names/icons with a screenshot taken before deploying; close it.
- [ ] **Step 4:** Commit `refactor(popup): read content apart from creating items`.

### Task 2: Load on a worker thread

**Files:** Modify `modules/Popup/ContentLoader.{hpp,cpp}`, `modules/Popup/ContentPopup.{hpp,cpp}`, `modules/Popup/Popup.{hpp,cpp}`

**Interfaces — Consumes:** Task 1. **Produces:**
```cpp
namespace ContentLoader {
  UINT LoadedMessage();   // RegisterWindowMessageW(L"LiteStepTNG.PopupContentLoaded"); lParam = ContentResult*, wParam = generation
  // Copies request; thread: CoInitializeEx(STA), Read, PostMessage(target, LoadedMessage(), generation, result);
  // on post failure result->Free() + delete. Pins the module. cancel may be bumped by the caller at any time.
  bool Start(HWND target, ContentRequest request, WPARAM generation, std::shared_ptr<volatile LONG> cancel);
}
void Popup::Refit();      // if visible: sized=false, re-run the show placement with the last position/owner
```
- `ContentPopup` state: `bool loading`, `WPARAM generation`, `std::shared_ptr<volatile LONG> cancel`.
- `PreShow`, not loaded and source != `THEMES`: `++generation`, add `LoadingItem` (Task 3; in this task a plain `InfoItem` with the loading text), `Start(...)`; on `Start` failure fall back to the synchronous path. `loaded` becomes true only on arrival.
- Arrival (`HandleMessage`, `message == LoadedMessage()`): if `wParam != generation` → `Free` + delete. Else delete the loading row, `CreateItems`, sort as today, `loaded = true`, `loading = false`, `Refit()`.
- `PostClose` while loading and not `keepLoaded`: set `*cancel`, `++generation`, remove the loading row, `loading=false`. Kept popups let the load finish (result applied while hidden; `Refit` does nothing then).
- `~ContentPopup` / `Unload`: set `*cancel`, `++generation`.
- `Popup::Show(LPRECT, Popup*)` stores `mLastPosition`/owner for `Refit`.

- [ ] **Step 1:** Implement. **Step 2:** build solution → exit 0, no `error`. **Step 3:** Commit `feat(popup): load content popups on a worker thread`.

### Task 3: Loading row with spinner

**Files:** Create `modules/Popup/LoadingItem.{hpp,cpp}` (+ vcxproj/filters); modify `ContentPopup.cpp`, `themes/LiteStep Enhanced/theme.rc`

**Interfaces — Consumes:** `Window::RequestFrames/StopFrames`, `FrameClock::Message()`, `IPainter`, `InfoItem`.
- `class LoadingItem : public InfoItem, public IPainter` — ctor `(Drawable *parent, LPCTSTR text)`; info-row skin (`mInfoWindowSettings`), starts with empty text, `RequestFrames(this)`. On each frame: after 150 ms (`StopWatch`) set the text once and advance the arc angle (one turn per 800 ms), `Repaint()`. Destructor: `StopFrames`, remove the painter.
- Painter: arc of 270°, radius 4 px, stroke 1.5 px, colour = the info item's font colour (`mInfoStateRender` base state, fall back to white), placed 10 px from the item's left edge, vertically centred; drawn only after the 150 ms.
- Text: `mSettings->GetString(L"LoadingText", L"Loading...")` of the popup.
- Theme: `MMMenuLoadingText "CARGANDO"` next to the other `MMMenu*` keys.

- [ ] **Step 1:** Implement. **Step 2:** build → exit 0. **Step 3:** Commit `feat(popup): themed loading row with a spinner`.

### Task 4: Verify on the running shell, document

- [ ] **Step 1:** Deploy all (`.\tools\deploy.ps1`), litestep alive.
- [ ] **Step 2 (responsiveness):** script that opens a slow popup by bang (`!PopupFolder:"C:\Windows\System32"` if a bang form exists, else `!PopupApps` after `!Recycle` so its cache is cold) and meanwhile, every 20 ms for 3 s, times `SendMessageTimeout(<litestep main window>, WM_NULL, ..., SMTO_ABORTIFHUNG, 1000)`. Expected: max < 50 ms. Run the same script against the master build first (deploy master, measure, redeploy branch) to record the before value.
- [ ] **Step 3:** Screenshots: loading row (CARGANDO + arc) during the load; the filled popup after.
- [ ] **Step 4:** Close mid-load (post `WM_ACTIVATE(WA_INACTIVE)`), then re-open: one copy of every item. `!Recycle` mid-load: litestep alive. `.\tools\smoke-test.ps1` → exit 0.
- [ ] **Step 5:** AGENTS.md §6 bullet "Content popups load off the UI thread" (ContentLoader, generation, LoadingItem, LoadingText). Commit `docs: describe asynchronous content popups`.
