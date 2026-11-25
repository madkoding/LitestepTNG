# Animations paced by the monitor refresh (FrameClock)

## Why
Every animation in nModules is driven by `SetTimer` (10 or 15 ms, which Windows rounds to its
~15.6 ms tick) or by a free-running `PostMessage` repaint loop. Neither is aligned with the
display refresh, so steps land unevenly (judder at 60 Hz, half the frames missing at 144 Hz)
or are rendered and thrown away. The user sees it in all of them.

## Goal / non-goals
- Goal: the four animations advance exactly once per display refresh:
  1. window entry (`ShowAnimation*`, popups) — `ModuleKit/Window.cpp` `SHOW_ANIMATION_TIMER`
  2. move/resize (`SetAnimation`) — `WM_ANIMATIONPAINT` repaint loop in `Window.cpp`
  3. taskbar button slide — `SetCallbackTimer(15)` in `TaskBar/Taskbar.cpp`
  4. desktop switch — `SetTimer(10)` in `VWM/Transition.cpp`
- Non-goals: no change to curves, durations or theme settings; no DirectComposition;
  `Desk/DesktopPainter.cpp` (not loaded on top of Explorer) is left alone.
- Anti-goal: an idle shell must not wake per frame — no thread, no messages when nothing animates.

## Approaches
- **A. FrameClock in ModuleKit (chosen):** a worker thread loops on `DwmFlush()` while there
  are subscribers and posts a message to each subscribed HWND per composition. Small,
  adapts to any refresh rate. Steps still run on litestep's UI thread.
- B. DirectComposition: compositor-driven, immune to a busy UI thread, but rewrites
  ModuleKit's rendering. Too large/risky for the gain.
- C. `timeBeginPeriod(1)`: trivial but still unaligned; higher power use.
The user asked for the recommended one (A).

## Design
- `ModuleKit/FrameClock.{hpp,cpp}`:
  `FrameClock::Subscribe(HWND)` / `Unsubscribe(HWND)`; message `WM_FRAMETICK`
  (private, registered with `RegisterWindowMessage` so it is the same in every DLL and never
  collides with `LM_*`). First subscriber starts the thread; it exits when the set is empty.
  The thread does `DwmFlush()`; if that fails (DWM off, session locked) it falls back to
  `Sleep(16)`. Posting is coalesced: a window gets at most one tick in its queue at a time
  (flag cleared when it is handled), so a busy UI thread skips frames instead of piling them up.
- Each animation subscribes when it starts, steps on `WM_FRAMETICK` (progress still computed
  from elapsed time), unsubscribes when it ends or when its window is destroyed.
  - `Window`: both show-animation and `SetAnimation` share one subscription of the top-level
    window; the `WM_ANIMATIONPAINT` self-post becomes "invalidate on the next tick".
  - Taskbar: subscribes its window instead of `SetCallbackTimer(15)`.
  - VWM: subscribes its message window instead of `SetTimer(kTimer, 10)`.
- ModuleKit is a static lib, so each DLL has its own clock instance; that is acceptable
  (threads only live during an animation).

## Left to the builder
- Exact thread-safety primitive for the subscriber set (critical section is fine).
- Verification: build whole solution (ModuleKit change), deploy, run `tools\smoke-test.ps1`,
  trigger `!VWMNext`/`!VWMPrev` and a popup by bang, and confirm with a frame-timing log
  (temporary) that steps arrive at the refresh interval; remove the log afterwards.
