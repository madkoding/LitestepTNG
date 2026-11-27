/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *  nTask.cpp
 *  The nModules Project
 *
 *  Main .cpp file for the nTask module.
 *  
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#include "../ModuleKit/LiteStep.h"
#include "../ModuleKit/LSModule.hpp"
#include "../ModuleKit/ErrorHandler.h"
#include "../CoreCom/Core.h"
#include "Constants.h"
#include "Task.h"
#include "TestWindow.hpp"
#include "Taskbar.hpp"
#include "Version.h"
#include "WindowManager.h"
#include "MinimizedShelf.hpp"
#include "../ModuleKit/MonitorInfo.hpp"

#include <algorithm>
#include <set>
#include <strsafe.h>
#include <string>
#include <vector>


using std::map;

// The LSModule class
LSModule gLSModule(TEXT(MODULE_NAME), TEXT(MODULE_AUTHOR), MakeVersion(MODULE_VERSION));

// The messages we want from the core
const UINT gLSMessages[] = { LM_GETREVID, LM_REFRESH, LM_FULLSCREENACTIVATED,
    LM_FULLSCREENDEACTIVATED, 0 };
const UINT sCoreMessages[] = { NCORE_DISPLAYCHANGE, NCORE_SETTINGCHANGE, 0 };

// All the taskbars we currently have loaded
TaskbarMap gTaskbars;

// Set to whether or not windows are activated by hovering over them. In this
// mode we should automatically move the mouse cursor to the center of the
// window when activated.
BOOL gActiveWindowTracking = FALSE;

// Minimize and restore events, for the minimized-windows shelf.
static HWINEVENTHOOK sMinimizeHook = nullptr;

static void CALLBACK MinimizeEvent(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG child, DWORD, DWORD)
{
    if (!gMinimizedShelf || object != OBJID_WINDOW || child != CHILDID_SELF)
    {
        return;
    }
    if (event == EVENT_SYSTEM_MINIMIZESTART)
    {
        gMinimizedShelf->Add(window);
    }
    else
    {
        gMinimizedShelf->Remove(window);
    }
}


/// <summary>
/// Creates the minimized-windows shelf if the theme turns it on (MinimizedShelfEnabled).
/// </summary>
static void StopShelf();
static void RebuildTaskbars();

static void StartShelf()
{
    if (!Settings(L"MinimizedShelf").GetBool(L"Enabled", false))
    {
        return;
    }
    gMinimizedShelf.reset(new MinimizedShelf());
    sMinimizeHook = SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, nullptr,
        MinimizeEvent, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}


//-------------------------------------------------------------------------------------------------
// A taskbar on every monitor.
//
// <Taskbar>EveryMonitor true: with more than one monitor, the taskbar keeps the primary
// monitor's windows and a copy of it, <Taskbar>Screen<n>, lists the windows of monitor n
// (MonitorInfo numbering: 0 is the primary). A copy takes every setting of its taskbar
// through Group, with X/Y/Width/Height worked out against its own monitor (top-level windows
// otherwise measure percentages against the primary monitor), so "100%-26" is the bottom of
// each screen; <Taskbar>ScreenX/ScreenY/ScreenWidth/ScreenHeight give the copies a geometry
// of their own. Explorer's secondary taskbars are hidden by nCore, so without this the other
// monitors had no taskbar at all.
// <Taskbar>ReserveScreen true|false (default true): the copies keep their edge of their
// monitor out of the work area, so maximized windows stop at the bar, as Core does for the
// primary monitor with ReserveScreen*.
//-------------------------------------------------------------------------------------------------
static std::set<std::wstring> sMonitorCopies;       // names of the copies
static std::vector<RECT> sMonitorLayout;            // the monitors the copies were made for
struct SavedWorkArea { HMONITOR monitor; RECT workArea; };
static std::vector<SavedWorkArea> sSavedWorkAreas;  // what the work areas were before us
static const UINT_PTR TIMER_COPY_WORKAREAS = 0x5EC1;
static UINT sRebuildMessage = 0;                    // posted when the monitors change

static std::vector<RECT> CurrentMonitorLayout()
{
    std::vector<RECT> layout;
    for (const MonitorInfo::Monitor &monitor : nCore::FetchMonitorInfo().GetMonitors())
    {
        layout.push_back(monitor.rect);
    }
    return layout;
}


static void SetTaskbarVariable(LPCWSTR name, LPCWSTR key, LPCWSTR value)
{
    WCHAR fullKey[MAX_RCCOMMAND];
    StringCchPrintfW(fullKey, _countof(fullKey), L"%s%s", name, key);
    LiteStep::LSSetVariable(fullKey, value);
}


static void SetTaskbarPixels(LPCWSTR name, LPCWSTR key, float value)
{
    WCHAR text[32];
    StringCchPrintfW(text, _countof(text), L"%d", int(value + (value < 0 ? -0.5f : 0.5f)));
    SetTaskbarVariable(name, key, text);
}


/// <summary>
/// Creates the copy of a taskbar for one monitor.
/// </summary>
static void CreateMonitorCopy(LPCWSTR taskbarName, const Settings &settings, UINT monitorIndex)
{
    const MonitorInfo::Monitor &monitor = nCore::FetchMonitorInfo().GetMonitor(monitorIndex);
    WCHAR copyName[MAX_RCCOMMAND];
    StringCchPrintfW(copyName, _countof(copyName), L"%sScreen%u", taskbarName, monitorIndex);
    if (gTaskbars.find(copyName) != gTaskbars.end())
    {
        return;
    }

    // ScreenX/Y/Width/Height, else the taskbar's own geometry, measured against this monitor
    // (a primary taskbar often leaves room for a start button or a tray the copies lack).
    auto distance = [&settings] (LPCWSTR copyKey, LPCWSTR key, float defaultValue, float length) {
        return settings.GetDistance(copyKey, settings.GetDistance(key, defaultValue)).Evaluate(length);
    };
    const float width = distance(L"ScreenWidth", L"Width", float(GetSystemMetrics(SM_CXSCREEN)), float(monitor.width));
    const float height = distance(L"ScreenHeight", L"Height", 36.0f, float(monitor.height));
    const float x = distance(L"ScreenX", L"X", 0.0f, float(monitor.width));
    const float y = distance(L"ScreenY", L"Y", 0.0f, float(monitor.height));

    WCHAR index[16];
    StringCchPrintfW(index, _countof(index), L"%u", monitorIndex);
    SetTaskbarVariable(copyName, L"Group", taskbarName);
    SetTaskbarVariable(copyName, L"Monitor", index);
    SetTaskbarPixels(copyName, L"X", monitor.rect.left + x);
    SetTaskbarPixels(copyName, L"Y", monitor.rect.top + y);
    SetTaskbarPixels(copyName, L"Width", width);
    SetTaskbarPixels(copyName, L"Height", height);

    gTaskbars.emplace(std::piecewise_construct, std::forward_as_tuple(copyName), std::forward_as_tuple(copyName));
    sMonitorCopies.insert(copyName);
}


/// <summary>
/// Keeps each copy's edge of its monitor out of that monitor's work area.
/// </summary>
static void EnforceCopyWorkAreas()
{
    struct Wanted { HMONITOR monitor; RECT monitorRect; RECT workArea; };
    std::vector<Wanted> wanted;

    for (const std::wstring &name : sMonitorCopies)
    {
        auto taskbar = gTaskbars.find(name);
        if (taskbar == gTaskbars.end() || !Settings(name.c_str()).GetBool(L"ReserveScreen", true))
        {
            continue;
        }
        HWND window = taskbar->second.GetWindow()->GetWindowHandle();
        RECT bar;
        if (!IsWindowVisible(window) || !GetWindowRect(window, &bar))
        {
            continue;
        }
        HMONITOR monitor = MonitorFromRect(&bar, MONITOR_DEFAULTTONULL);
        MONITORINFO info = { sizeof(info) };
        if (monitor == nullptr || !GetMonitorInfoW(monitor, &info))
        {
            continue;
        }
        auto entry = std::find_if(wanted.begin(), wanted.end(), [monitor] (const Wanted &w) { return w.monitor == monitor; });
        if (entry == wanted.end())
        {
            wanted.push_back({ monitor, info.rcMonitor, info.rcMonitor });
            entry = wanted.end() - 1;
        }

        // The edge the bar is nearest to, along its long side.
        const RECT &m = info.rcMonitor;
        RECT &work = entry->workArea;
        if (bar.right - bar.left >= bar.bottom - bar.top)
        {
            if (bar.top - m.top < m.bottom - bar.bottom)
            {
                work.top = std::max(work.top, bar.bottom);
            }
            else
            {
                work.bottom = std::min(work.bottom, bar.top);
            }
        }
        else if (bar.left - m.left < m.right - bar.right)
        {
            work.left = std::max(work.left, bar.right);
        }
        else
        {
            work.right = std::min(work.right, bar.left);
        }
    }

    for (Wanted &w : wanted)
    {
        MONITORINFO info = { sizeof(info) };
        if (w.workArea.right <= w.workArea.left || w.workArea.bottom <= w.workArea.top ||
            !GetMonitorInfoW(w.monitor, &info))
        {
            continue;
        }
        bool saved = std::any_of(sSavedWorkAreas.begin(), sSavedWorkAreas.end(),
            [&w] (const SavedWorkArea &s) { return s.monitor == w.monitor; });
        if (!saved)
        {
            sSavedWorkAreas.push_back({ w.monitor, info.rcWork });
        }
        if (!EqualRect(&info.rcWork, &w.workArea))
        {
            // Not broadcast, as Core does: Explorer would recompute its own value at once.
            SystemParametersInfoW(SPI_SETWORKAREA, 0, &w.workArea, 0);
        }
    }
}


static void RestoreCopyWorkAreas()
{
    for (SavedWorkArea &saved : sSavedWorkAreas)
    {
        MONITORINFO info = { sizeof(info) };
        if (GetMonitorInfoW(saved.monitor, &info))
        {
            SystemParametersInfoW(SPI_SETWORKAREA, 0, &saved.workArea, SPIF_SENDCHANGE);
        }
    }
    sSavedWorkAreas.clear();
}


static void StartCopyWorkAreas()
{
    if (!sMonitorCopies.empty())
    {
        EnforceCopyWorkAreas();
        SetTimer(gLSModule.GetMessageWindow(), TIMER_COPY_WORKAREAS, 1000, nullptr);
    }
}


static void StopCopyWorkAreas()
{
    KillTimer(gLSModule.GetMessageWindow(), TIMER_COPY_WORKAREAS);
    RestoreCopyWorkAreas();
}


/// <summary>
/// Throws every taskbar away and makes them again (a refresh, or the monitors changed).
/// </summary>
static void RebuildTaskbars()
{
    StopShelf();
    StopCopyWorkAreas();
    WindowManager::Stop();
    DestroyTaskbars();
    WindowManager::Start();
    LoadSettings();
    StartCopyWorkAreas();
    StartShelf();
    WindowManager::AddExisting();
}


static void StopShelf()
{
    if (sMinimizeHook != nullptr)
    {
        UnhookWinEvent(sMinimizeHook);
        sMinimizeHook = nullptr;
    }
    gMinimizedShelf.reset();
}

/// <summary>
/// Called by the LiteStep core when this module is loaded.
/// </summary>
EXPORT_CDECL(int) initModuleW(HWND parent, HINSTANCE instance, LPCWSTR /* path */)
{
    if (!gLSModule.Initialize(parent, instance))
    {
        return 1;
    }
    
    if (!gLSModule.ConnectToCore(MakeVersion(CORE_VERSION)))
    {
        return 1;
    }
    nCore::RegisterForCoreMessages(gLSModule.GetMessageWindow(), sCoreMessages);

    SystemParametersInfo(SPI_GETACTIVEWINDOWTRACKING, 0, &gActiveWindowTracking, 0);

    sRebuildMessage = RegisterWindowMessageW(L"LiteStepTNG.TaskbarRebuild");

    // Load settings
    LoadSettings();
    StartCopyWorkAreas();

    // Start the window manager
    WindowManager::Start();
    StartShelf();
     
    // Register a bang command for creating our test window
    LiteStep::AddBangCommand(L"!nTaskTestWindow", [] (HWND, LPCTSTR) -> void
    {
        TestWindow::Create();
    });

    return 0;
}


/// <summary>
/// Called by the core when this module is about to be unloaded.
/// </summary>
void quitModule(HINSTANCE /* hDllInstance */)
{
    LiteStep::RemoveBangCommand(L"!nTaskTestWindow");

    TestWindow::DestroyAll();
    StopShelf();
    StopCopyWorkAreas();
    WindowManager::Stop();
    DestroyTaskbars();
    gLSModule.DeInitalize();
}


/// <summary>
/// Handles the main window's messages.
/// </summary>
/// <param name="hWnd">The window the message is for.</param>
/// <param name="uMsg">The type of message.</param>
/// <param name="wParam">wParam</param>
/// <param name="lParam">lParam</param>
LRESULT WINAPI LSMessageHandler(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch(message)
    {
    case WM_CREATE:
        {
            // Add the existing windows in a little bit so we dont hinder startup.
            SetTimer(window, TIMER_ADD_EXISTING, 50, NULL);

            SendMessage(LiteStep::GetLitestepWnd(), LM_REGISTERMESSAGE, (WPARAM)window, (LPARAM)gLSMessages);
        }
        return 0;

    case NCORE_SETTINGCHANGE:
      if (wParam == SPI_SETACTIVEWINDOWTRACKING) {
        SystemParametersInfo(SPI_GETACTIVEWINDOWTRACKING, 0, &gActiveWindowTracking, 0);
      }
      return 0;

    case WM_DESTROY:
        {
            SendMessage(LiteStep::GetLitestepWnd(), LM_UNREGISTERMESSAGE, (WPARAM)window, (LPARAM)gLSMessages);
        }
        return 0;

    case WM_TIMER:
        {
            switch(wParam)
            {
            case TIMER_ADD_EXISTING:
                {
                    KillTimer(window, TIMER_ADD_EXISTING);
                    WindowManager::AddExisting();
                }
                return 0;

            case TIMER_COPY_WORKAREAS:
                EnforceCopyWorkAreas();
                return 0;

            default:
                {
                }
                return WindowManager::ShellMessage(window, message, wParam, lParam);
            }
        }
        break;

    case LM_FULLSCREENACTIVATED:
        {
            for (auto &taskbar : gTaskbars)
            {
                taskbar.second.GetWindow()->FullscreenActivated((HMONITOR) wParam, (HWND) lParam);
            }
        }
        return 0;

    case LM_FULLSCREENDEACTIVATED:
        {
            for (auto &taskbar : gTaskbars)
            {
                taskbar.second.GetWindow()->FullscreenDeactivated((HMONITOR) wParam);
            }
        }
        return 0;

    case LM_REFRESH:
        RebuildTaskbars();
        return 0;

    case NCORE_DISPLAYCHANGE:
        {
            // Monitors added, removed or moved: the copies are made for the old layout.
            LRESULT result = WindowManager::ShellMessage(window, message, wParam, lParam);
            if (!sMonitorLayout.empty() && CurrentMonitorLayout().size() > 0)
            {
                std::vector<RECT> layout = CurrentMonitorLayout();
                bool changed = layout.size() != sMonitorLayout.size();
                for (size_t i = 0; !changed && i < layout.size(); ++i)
                {
                    changed = !EqualRect(&layout[i], &sMonitorLayout[i]);
                }
                if (changed)
                {
                    PostMessage(window, sRebuildMessage, 0, 0);
                }
            }
            return result;
        }

    case WM_ADDED_EXISTING:
        if (gMinimizedShelf)
        {
            gMinimizedShelf->Start();
        }
        return WindowManager::ShellMessage(window, message, wParam, lParam);

    case LM_GETMINRECT:
    case LM_LANGUAGE:
    case LM_REDRAW:
    case LM_WINDOWACTIVATED:
    case LM_WINDOWCREATED:
    case LM_WINDOWDESTROYED:
    case LM_WINDOWREPLACED:
    case LM_WINDOWREPLACING:
    case LM_MONITORCHANGED:
    case LM_TASK_SETPROGRESSSTATE:
    case LM_TASK_SETPROGRESSVALUE:
    case LM_TASK_MARKASACTIVE:
    case LM_TASK_REGISTERTAB:
    case LM_TASK_UNREGISTERTAB:
    case LM_TASK_SETACTIVETAB:
    case LM_TASK_SETTABORDER:
    case LM_TASK_SETTABPROPERTIES:
    case LM_TASK_SETOVERLAYICON:
    case LM_TASK_SETOVERLAYICONDESC:
    case LM_TASK_SETTHUMBNAILTOOLTIP:
    case LM_TASK_SETTHUMBNAILCLIP:
    case LM_TASK_THUMBBARADDBUTTONS:
    case LM_TASK_THUMBBARUPDATEBUTTONS:
    case LM_TASK_THUMBBARSETIMAGELIST:
      return WindowManager::ShellMessage(window, message, wParam, lParam);
    }
    if (message == sRebuildMessage && sRebuildMessage != 0)
    {
        RebuildTaskbars();
        return 0;
    }
    return DefWindowProc(window, message, wParam, lParam);
}


/// <summary>
/// Destroys all taskbars.
/// </summary>
void DestroyTaskbars()
{
    gTaskbars.clear();
    sMonitorCopies.clear();
    sMonitorLayout.clear();
}


/// <summary>
/// Reads through the .rc files and creates Taskbars.
/// </summary>
void LoadSettings()
{
    LiteStep::IterateOverLineTokens(L"*nTaskbar", CreateTaskbar);
}


/// <summary>
/// Creates a new taskbar
/// </summary>
void CreateTaskbar(LPCTSTR taskbarName)
{
    if (gTaskbars.find(taskbarName) == gTaskbars.end())
    {
        Settings settings(taskbarName);
        const UINT monitors = nCore::FetchMonitorInfo().GetMonitorCount();
        const bool everyMonitor = settings.GetBool(L"EveryMonitor", false) && monitors > 1;
        if (everyMonitor && settings.GetMonitor(L"Monitor", 0xFFFFFFFF) == 0xFFFFFFFF)
        {
            // The other monitors get their own copy: this one keeps the primary's windows.
            SetTaskbarVariable(taskbarName, L"Monitor", L"0");
        }

        gTaskbars.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(taskbarName),
            std::forward_as_tuple(taskbarName)
            );

        if (everyMonitor)
        {
            for (UINT monitor = 1; monitor < monitors; ++monitor)
            {
                CreateMonitorCopy(taskbarName, settings, monitor);
            }
            sMonitorLayout = CurrentMonitorLayout();
        }
    }
    else
    {
        ErrorHandler::Error(ErrorHandler::Level::Critical, L"Attempt to (re)create the already existing taskbar %s!", taskbarName);
    }
}

