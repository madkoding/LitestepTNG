//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//
// This is a part of the Litestep Shell source code.
//
// Copyright (C) 1997-2015  LiteStep Development Team
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
#if !defined(LITESTEP_H)
#define LITESTEP_H

#include "../utility/ILiteStep.h"
#include "../utility/common.h"
#include "../lsapi/lsapidefines.h"
#include <vector>


// forward declarations
class IService;
class TrayService;
class FullscreenMonitor;
class DataStore;
class MessageManager;
class ModuleManager;
namespace litestep
{
namespace themev2
{
    class ThemeEngineV2;
}
}



// RegisterShellHook flags
#define RSH_UNREGISTER  0
#define RSH_REGISTER    1
#define RSH_PROGMAN     2
#define RSH_TASKMAN     3

// Program Options
const TCHAR szMainWindowClass[] = _T("TApplication");
const TCHAR szMainWindowTitle[] = _T("LiteStep");

#define GWL_CLASSPOINTER 0


// Litestep Startup Flags (LSF)
#define LSF_ALTERNATE_CONFIG   0x0004

int StartLitestep(HINSTANCE hInst, WORD wStartFlags, LPCTSTR pszAltConfigFile);


class CLiteStep: public ILiteStep
{
public:
    CLiteStep();
    ~CLiteStep();

    HRESULT Start(HINSTANCE hInstance);
    HRESULT Stop();

    int Run();

    static LRESULT CALLBACK ExternalWndProc(
        HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    // ILiteStep
    void PeekAllMsgs();

private:
    void MessageHandler(MSG &message);

    HRESULT CreateMainWindow();
    HRESULT DestroyMainWindow();

    void _RegisterShellNotifications(HWND hWnd);
    void _UnregisterShellNotifications(HWND hWnd);

    LRESULT InternalWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT _HandleSessionChange(DWORD dwCode, DWORD dwSession);

    void _Recycle();
    HRESULT _EnumRevIDs(LSENUMREVIDSPROCW pfnCallback, LPARAM lParam) const;

    // Application instance
    HINSTANCE m_hInstance;

    // Recovery Menu
    // This is a special service that is not kept in the services array
    IService* m_pRecoveryMenu;

    // Windows
    HWND m_hMainWindow; // = NULL;

    // Session shell: true when no one owned the shell window at start-up (Windows
    // started LiteStep as the shell, Explorer is not running).
    bool m_bSessionShell; // = false
    bool m_bHandShellToExplorer; // = false; set by a deliberate quit
    HANDLE m_hShellHost; // = nullptr; ShellAppRuntime.exe, started by _StartShellHost
    HANDLE _StartShellHost();
    void _HandShellToExplorer();

    // Notifications (Shell Hooks + WTS notifications)
    typedef BOOL (WINAPI* RSHPROC)(HWND, DWORD);

    RSHPROC m_pRegisterShellHook;
    UINT WM_ShellHook; // = 0;
    HMODULE m_hWtsDll;

    // Function blocks
    UINT m_BlockRecycle; // = 0

    //
    // Manager Related
    //
    void _HandleDisplayChange(UINT bitsPerPixel, UINT width, UINT height);
    void _ScheduleUiRecycle(const wchar_t* reason);
    ModuleManager* m_pModuleManager; // = NULL;
    litestep::themev2::ThemeEngineV2* m_pThemeEngineV2; // Theme V2 engine
    bool m_hasPendingDisplayChangeRecycle;
    DataStore* m_pDataStoreManager; // = NULL;
    MessageManager* m_pMessageManager; // = NULL;

    HRESULT _InitManagers();
    HRESULT _StartManagers();
    HRESULT _StopManagers();
    void _CleanupManagers();

    bool m_bSignalExit; // = false
    int m_nQuitMsg;

    //
    // Service Related
    //
    TrayService* m_pTrayService; // = NULL;
    FullscreenMonitor* m_pFullscreenMonitor; // = nullptr;
    std::vector<IService*> m_Services;

    HRESULT _InitServices();
    HRESULT _StartServices();
    HRESULT _StopServices();
    void _CleanupServices();
};

#endif // LITESTEP_H
