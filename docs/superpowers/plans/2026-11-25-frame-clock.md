# FrameClock Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every nModules animation advances once per display refresh instead of on `SetTimer` ticks or a free-running repaint loop.

**Architecture:** A `FrameClock` in ModuleKit runs a worker thread looping on `DwmFlush()` while any HWND is subscribed and posts one coalesced registered message per composition. `Window` owns the subscription for its top-level HWND and forwards ticks to its own animations and to `MessageHandler`s that asked for frames (Taskbar). VWM subscribes its message window directly.

**Tech Stack:** C++17/MSVC, Win32, `dwmapi` (`DwmFlush`), MSBuild `litestep.sln` Release|x64.

**Spec:** `docs/superpowers/specs/2026-11-25-frame-clock-design.md`

## Global Constraints
- No change to curves, durations or theme settings; `Desk/` untouched.
- Idle shell: no clock thread and no posted messages when nothing animates.
- Tick message comes from `RegisterWindowMessageW(L"LiteStepTNG.FrameTick")` (same value in every DLL, outside the `LM_` range).
- At most one tick queued per HWND (coalescing); a busy UI thread drops frames, never queues them.
- `DwmFlush()` failure → `Sleep(16)` and keep going.
- ModuleKit change ⇒ build the whole solution. No unit-test framework exists: verification is build + deploy + runtime timing log (removed afterwards) + `tools\smoke-test.ps1`.
- Never synthesize mouse input; trigger everything by bang (`litestep.exe !Bang`).

## Review Focus
- Window destroyed while subscribed (popup closed mid-animation, module unloaded): the clock must drop it (PostMessage fails / `Unsubscribe` in `~Window`), never post to a recycled HWND forever. → Task 2 step "destroy mid-animation".
- Module unload (recycle, `!Quit`) while the thread runs: thread must stop and be joined before the DLL's code is unmapped. → Task 1 `FrameClock::Shutdown()` called from `~Window`-independent unload path (Task 2) and VWM `quitModule` (Task 4).
- Animation restarted while running (`Relayout` during a slide, `!VWMNext` twice fast, re-show): subscription must stay single, not double-ticked. → Subscribe is idempotent (Task 1).
- Session locked / DWM composition paused: `DwmFlush` returns error immediately → must not spin at 100 % CPU. → `Sleep(16)` fallback (Task 1).
- Child window animation (`SetAnimation` on an overlay child via bangs): today relies on a self-invalidating paint loop that `ValidateRect(nullptr)` can stop; ticks must repaint the parent while any child animates. → Task 2.

---

### Task 1: FrameClock

**Files:**
- Create: `modules/ModuleKit/FrameClock.hpp`, `modules/ModuleKit/FrameClock.cpp`
- Modify: `modules/ModuleKit/ModuleKit.vcxproj` (+ `.filters`) — add both files next to `Window.*`

**Interfaces — Produces:**
```cpp
namespace FrameClock {
  UINT Message();               // registered "LiteStepTNG.FrameTick", cached
  void Subscribe(HWND window);  // idempotent; starts the thread on first subscriber
  void Unsubscribe(HWND window);// idempotent; thread exits when the set is empty
  void Ack(HWND window);        // call first thing when handling Message(); re-arms posting
  void Shutdown();              // clears all, stops and joins the thread (module unload)
}
```
- Subscriber set: `std::map<HWND,bool /*pending*/>` under a `CRITICAL_SECTION`/`std::mutex`.
- Thread loop: `if (FAILED(DwmFlush())) Sleep(16);` then, under the lock, for each entry with `!pending`: `PostMessage(hwnd, Message(), 0, 0)`; on failure (or `!IsWindow`) erase it, else set pending. Exit when the set is empty; `Subscribe` restarts it if needed (use `std::thread` + flag; join a finished thread before starting a new one).
- `#pragma comment(lib, "dwmapi.lib")` in the .cpp.

- [ ] **Step 1:** Write the two files and add them to the project.
- [ ] **Step 2:** Build ModuleKit: `msbuild modules\ModuleKit\ModuleKit.vcxproj /p:Configuration=Release /p:Platform=x64 /m /v:minimal /nologo` → no `error`.
- [ ] **Step 3:** Commit `feat(modulekit): add FrameClock, a DwmFlush-paced tick`.

### Task 2: Window animations on FrameClock

**Files:** Modify `modules/ModuleKit/Window.hpp`, `modules/ModuleKit/Window.cpp`

**Interfaces — Consumes:** Task 1. **Produces:**
```cpp
void Window::RequestFrames(MessageHandler* handler); // handler gets HandleMessage(hwnd, FrameClock::Message(), 0, 0, ...) every frame
void Window::StopFrames(MessageHandler* handler);
```
Child windows delegate both to `mParent` (same pattern as `SetCallbackTimer`).

- Private `void UpdateFrameSubscription()` on the top-level window: subscribed iff `mShowAnimating || mAnimating || mChildAnimating || !mFrameHandlers.empty()`. New members `bool mChildAnimating`, `std::set<MessageHandler*> mFrameHandlers`.
- `BeginShowAnimation`: replace `SetTimer(SHOW_ANIMATION_TIMER…)` with `UpdateFrameSubscription()`; `EndShowAnimation`: replace `KillTimer` likewise. Remove `SHOW_ANIMATION_TIMER` and its `WM_TIMER` branch.
- `SetAnimation`: after `mAnimating = true`, top-level calls `UpdateFrameSubscription()`; a child calls its top-level ancestor's (set `mChildAnimating` there via the next paint, see below).
- `WM_PAINT`: stop calling `Animate()` there and drop the `PostMessage(WM_ANIMATIONPAINT)`; instead store `mChildAnimating = inAnimation` and `UpdateFrameSubscription()`. Keep the child `Animate()` call inside `Paint()`.
- Handle `FrameClock::Message()` at the top of `HandleMessage` (before the `WM_FIRSTREGISTERED` forwarding): `FrameClock::Ack(window)`; `StepShowAnimation()` if `mShowAnimating`; `Animate()` if `mAnimating` (top-level); `Repaint()` if `mChildAnimating`; call every frame handler; then `UpdateFrameSubscription()`.
- `WM_ANIMATIONPAINT` enum value and empty case stay (Desk still posts it).
- Destructor / `WM_DESTROY`: `FrameClock::Unsubscribe(window)`.
- Unload path: `LSModule` teardown (`modules/ModuleKit/LSModule.cpp`, where module windows are destroyed) calls `FrameClock::Shutdown()` last.

- [ ] **Step 1:** Implement as above.
- [ ] **Step 2:** Build the whole solution (`msbuild litestep.sln /p:Configuration=Release /p:Platform=x64 /m /verbosity:minimal /nologo`) → no `error`.
- [ ] **Step 3:** Commit `feat(modulekit): drive show and move animations from FrameClock`.

### Task 3: Taskbar slide on FrameClock

**Files:** Modify `modules/TaskBar/Taskbar.hpp`, `modules/TaskBar/Taskbar.cpp`

**Interfaces — Consumes:** `Window::RequestFrames/StopFrames`, `FrameClock::Message()`.

- Replace `UINT_PTR mAnimationTimer` with `bool mAnimationRunning`. `Relayout`: `RequestFrames(this)` when starting (idempotent via the set). The `WM_TIMER` branch becomes `if (message == FrameClock::Message() && mAnimationRunning)` with the same body; on finish `StopFrames(this)`. Destructor: `StopFrames(this)` if running.

- [ ] **Step 1:** Implement. **Step 2:** build solution → no `error`. **Step 3:** Commit `feat(taskbar): pace the button slide with FrameClock`.

### Task 4: VWM transition on FrameClock

**Files:** Modify `modules/VWM/Transition.{hpp,cpp}`, `modules/VWM/ModuleEntry.cpp`

- `Begin`: `FrameClock::Subscribe(timerWindow)` instead of `SetTimer(kTimer,10)`; end: `Unsubscribe` instead of `KillTimer`. Rename `OnTimer(HWND, UINT_PTR)` → `bool OnFrame(HWND)` (calls `FrameClock::Ack` first). `ModuleEntry.cpp` message proc: `if (message == FrameClock::Message() && Transition::OnFrame(window)) return 0;` replacing the `WM_TIMER` case. `quitModule`: `FrameClock::Shutdown()` after `Transition::Unload`. Remove `kTimer`.

- [ ] **Step 1:** Implement. **Step 2:** build solution → no `error`. **Step 3:** Commit `feat(vwm): pace the desktop transition with FrameClock`.

### Task 5: Verify on the running shell, document

- [ ] **Step 1 (temporary log):** in `FrameClock.cpp` thread loop append `QueryPerformanceCounter` deltas to `%TEMP%\frameclock.log` (one line per tick). Build, `.\tools\deploy.ps1`, confirm litestep alive.
- [ ] **Step 2:** Trigger by bang: `litestep.exe !VWMNext`, then `!VWMPrev`; a popup (`!PopupContext`, close with Escape-free `!PopupClose` if available, else wait); `!nLabelMove <a theme label> <x> <y> 400`. Expected: log deltas ≈ refresh interval (16.7 ms at 60 Hz, 6.9 ms at 144 Hz, per the monitor), and **no new lines** a second after each animation ends (idle check).
- [ ] **Step 3:** Screenshot after each bang: elements end exactly where they did before (no stranded layered/transparent popup, VWM overlay gone).
- [ ] **Step 4:** Remove the log, rebuild, deploy; `.\tools\smoke-test.ps1` → exit 0.
- [ ] **Step 5:** AGENTS.md §6: add a bullet "Animations paced by FrameClock" (what it is, the registered message, coalescing, that new animations must subscribe instead of using `SetTimer`). Commit `docs: describe FrameClock`.
