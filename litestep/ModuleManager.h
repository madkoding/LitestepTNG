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
#if !defined(MODULEMANAGER_H)
#define MODULEMANAGER_H

#include "module.h"
#include "ModuleResolver.h"
#include "../utility/IManager.h"
#include "../utility/common.h"
#include <list>


/** List of modules */
typedef std::list<Module*> ModuleQueue;


/**
 * Manages loaded modules.
 */
class ModuleManager: public IManager
{
public:
    /**
     * Constructor.
     */
    ModuleManager();

    /**
     * Destructor.
     */
    virtual ~ModuleManager();

    /**
     * Starts the module manager and loads all the modules specified in
     * <code>step.rc</code>.
     *
     * @param  pILiteStep  interface to LiteStep's core
     * @return <code>S_OK</code> if successful or an error code
     */
    HRESULT Start(ILiteStep *pILiteStep);

    /**
     * Unloads all modules and stops the module manager.
     *
     * @return <code>S_OK</code> if successful or an error code
     */
    HRESULT Stop();

    /**
     * Loads all the modules specified in <code>step.rc</code>.
     *
     * @return <code>S_OK</code> if successful or an error code
     */
    HRESULT rStart();

    /**
     * Unloads all modules.
     *
     * @return <code>S_OK</code> if successful or an error code
     */
    HRESULT rStop();

    /**
     * Loads a module.
     *
     * @param  pwzLocation  path to the module's DLL
     * @param  dwFlags      set of flags that control how the module is loaded
     * @return <code>TRUE</code> if successful or <code>FALSE</code> if an
     *         error occurs
     */
    BOOL LoadModule(LPCWSTR pwzLocation, DWORD dwFlags);

    /**
     * Unloads a module given its instance handle.
     *
     * @param  hModule  handle to the module's DLL instance
     * @return <code>TRUE</code> if successful or <code>FALSE</code> if an
     *         error occurs
     */
    BOOL QuitModule(HINSTANCE hModule);

    /**
     * Unloads a module given the path to its DLL.
     *
     * @param  pwzLocation  path to the module's DLL
     * @return <code>TRUE</code> if successful or <code>FALSE</code> if an
     *         error occurs
     */
    BOOL QuitModule(LPCWSTR pwzLocation);

    /**
     * Reloads a module given its instance handle
     *
     * @param  hModule  handle to the module's DLL instance
     * @return <code>TRUE</code> if successful or <code>FALSE</code> if an
     *         error occurs
     */
    BOOL ReloadModule(HINSTANCE hModule);

    /**
     * Enumerates loaded modules. Calls the callback function once for each
     * loaded module. Continues until all modules have been enumerated or the
     * callback function returns <code>FALSE</code>.
     *
     * @param  pfnCallback  pointer to callback function
     * @param  lParam       application-defined value passed to the callback
     *                      function
     * @return <code>S_OK</code> if all modules were enumerated,
     *         <code>S_FALSE</code> if the callback function returned
     *         <code>FALSE</code>, or an error code
     */
    HRESULT EnumModules(LSENUMMODULESPROCW pfnCallback, LPARAM lParam) const;

    /**
     * Enumerates performance statistics for loaded modules. Calls the callback
     * function once for each loaded module. Continues until all modules have
     * been enumerated or the callback function returns <code>FALSE</code>.
     *
     * @param  pfnCallback  pointer to callback function
     * @param  lParam       application-defined value passed to the callback
     *                      function
     * @return <code>S_OK</code> if all modules were enumerated,
     *         <code>S_FALSE</code> if the callback function returned
     *         <code>FALSE</code>, or an error code
     */
    HRESULT EnumPerformance(LSENUMPERFORMANCEPROCW pfnCallback, LPARAM lParam) const;

private:
    /**
     * Loads all the modules specified in <code>step.rc</code>.
     *
     * @return number of modules loaded
     */
    UINT _LoadModules();

    /**
     * Initializes the modules in the specified list.
     *
     * @param  mqModules  list of modules to initialize
     * @return number of modules initialized
     */
    UINT _StartModules(ModuleQueue& mqModules);

    /**
     * Unloads all loaded modules.
     */
    void _QuitModules();

    /**
     * Finds a module in the loaded module list based on the path to its DLL.
     *
     * @return iterator that points to the module or the end of the list
     */
    ModuleQueue::iterator _FindModule(LPCWSTR pwzLocation);

    /**
     * Finds a module in the loaded module list based on its instance handle.
     *
     * @return iterator that points to the module or the end of the list
     */
    ModuleQueue::iterator _FindModule(HINSTANCE hModule);

    /**
     * Refills m_aliases from the "*ModuleAlias" lines of the settings.
     *
     * @param  f  settings file handle from LCOpenW
     */
    void _ReadAliases(LPVOID f);

    /** m_sAppPath with a trailing backslash. */
    std::wstring _AppPathWithSlash() const;

    /**
     * The file a module location stands for, used to find duplicates: a
     * name with no folder is <app path><name>.dll (ModuleResolver::
     * ExpandLocation), then GetFullPathNameW. Never passed to LoadLibrary.
     */
    std::wstring _LocationKey(LPCWSTR pwzLocation) const;

    /**
     * Puts <app path>Core.dll at the front of a start-up queue when no
     * queued module is Core.dll and the file exists. Called by _LoadModules
     * when a classic name was mapped to an nModule (every nModule needs
     * nCore, and a 2002 theme never loads it).
     */
    void _AddCoreForNModules(ModuleQueue& mqModules);

    /**
     * Appends a module to a start-up queue, unless the queue already has
     * one for the same file (_LocationKey, compared case-insensitively).
     *
     * @param  mqModules  queue being built by _LoadModules
     * @param  pModule    module from _MakeModule (may be nullptr)
     */
    void _QueueModule(ModuleQueue& mqModules, Module* pModule);

    /**
     * Allocates a module object. The location first goes through the
     * "*ModuleAlias" map (config\module-map.rc), so a classic module name
     * loads the matching nModule.
     *
     * @param  pwzLocation  path to the module's DLL
     * @param  dwFlags      set of flags that control how the module is loaded
     * @param  bNetLine     true for *NetLoadModule / *NetInstallModule lines:
     *                      an unmapped name is expanded (ResolveNet), and the
     *                      module is skipped when the file is absent or is a
     *                      32-bit DLL
     * @param  pbMapped     set to true when the name was mapped (may be null)
     * @return pointer to the module object, or nullptr when it is skipped
     */
    Module* _MakeModule(LPCWSTR pwzLocation, DWORD dwFlags,
        bool bNetLine = false, bool* pbMapped = nullptr);

    /**
     * Waits for an array of events to all be set while remaining responsive
     * to user input.
     *
     * @param  pHandles  array of event handles
     * @param  dwCount   number of handles in the array
     */
    void _WaitForModules(const HANDLE* pHandles, size_t stCount) const;

    /** List of loaded modules */
    ModuleQueue m_ModuleQueue;

    /** Pointer to LiteStep's core interface */
    ILiteStep *m_pILiteStep;

    /** LiteStep's main window handle */
    HWND m_hLiteStep;

    /** Path to LiteStep's root directory */
    std::wstring m_sAppPath;

    /** Classic module names and the nModules that replace them */
    ModuleResolver::AliasMap m_aliases;

    /**
     * Predicate used by <code>_FindModule</code> to locate a loaded module
     * given the path to its DLL.
     */
    struct IsLocationEqual
    {
        /**
         * Constructor.
         */
        IsLocationEqual(LPCWSTR pwzLocation) : m_pwzLocation(pwzLocation)
        {
            // do nothing
        }

        /**
         * Returns <code>true</code> if specified module's DLL path matches
         * the path passed to the constructor.
         */
        bool operator() (const Module* pModule) const
        {
            return (_wcsicmp(m_pwzLocation, pModule->GetLocation()) == 0);
        }

    private:
        /** Path to match */
        LPCWSTR m_pwzLocation;
    };

    /**
     * Predicate used by <code>_FindModule</code> to locate a loaded module
     * given its DLL instance handle.
     */
    struct IsInstanceEqual
    {
        /**
         * Constructor.
         */
        IsInstanceEqual(HINSTANCE hInstance) : m_hInstance(hInstance)
        {
            // do nothing
        }

        /**
         * Returns <code>true</code> if specified module's DLL instance handle
         * matches the handle passed to the constructor.
         */
        bool operator() (const Module* pModule) const
        {
            return (pModule->GetInstance() == m_hInstance);
        }

    private:
        /** Instance handle to match */
        HINSTANCE m_hInstance;
    };
};


#endif // MODULEMANAGER_H
