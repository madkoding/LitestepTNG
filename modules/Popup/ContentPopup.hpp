/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *  ContentPopup.hpp
 *  The nModules Project
 *
 *  A popup which retrives its content from outside LiteStep's .rc files.
 *  
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma once

#include "Popup.hpp"
#include "FolderItem.hpp"
#include <ShlObj.h>

// Favorites: LiteStep's own list of pinned applications, a folder of shortcuts under the
// LiteStep directory. (Windows does not let programs pin to its taskbar, so this cannot be
// the taskbar's list; it only starts out as a copy of it.)
namespace Favorites
{
    // Path of the favorites folder with a trailing backslash; created and seeded from the
    // Windows taskbar pins the first time it is needed.
    bool GetFolder(LPWSTR path, size_t cchPath);

    // True if the file is inside the favorites folder.
    bool Contains(LPCWSTR file);

    // Copies a shortcut (or any file) into the favorites folder.
    bool Add(LPCWSTR file);

    // Adds an application from the shell's Applications folder, which has no file to
    // copy: a shortcut named after its title is created instead.
    bool AddApp(LPCWSTR appParsingName, LPCWSTR title);

    // Command prefix of items that launch an Applications-folder entry.
    extern const WCHAR AppCommandPrefix[];

    // Deletes a favorite. Only files inside the favorites folder are touched.
    bool Remove(LPCWSTR file);
}

class ContentPopup : public Popup {
public:
    enum ContentSource {
        PATH,
        ADMIN_TOOLS,
        CONTROL_PANEL,
        MY_COMPUTER,
        NETWORK,
        PRINTERS,
        RECENT_DOCUMENTS,
        RECYCLE_BIN,
        START_MENU,
        PROGRAMS,
        THEMES,     // Folders in $LiteStepDir$themes\ that contain a theme.rc
        PINNED,     // Favorites (LiteStep's own pin list, see namespace Favorites)
        APPS        // Every installed application (the shell's Applications folder)
    };

    explicit ContentPopup(ContentSource source, LPCTSTR title, LPCTSTR bang, LPCTSTR prefix);
    explicit ContentPopup(LPCTSTR path, bool dynamic, LPCTSTR title, LPCTSTR bang, LPCTSTR prefix);
    virtual ~ContentPopup();

    //
    LRESULT WINAPI HandleMessage(HWND, UINT, WPARAM, LPARAM, LPVOID);

    //
    void AddPath(LPCTSTR path);

protected:
    void PreShow() override;
    virtual void PostClose() override;

private:

    //
    void LoadContent();

    //
    void LoadShellFolder(GUID folder, bool dontExpandFolders = false);

    //
    void LoadPath(LPCTSTR path);

    // Lists the installed themes; each item runs !ThemeSwitch <name>.
    void LoadThemes();

    // Lists the favorites.
    void LoadPinned();

    // Lists every installed application, classic and packaged.
    void LoadApps();

    // Lists the most recently used documents, newest first.
    void LoadRecent();

    // Frees the items and stops watching the folders; the next show loads again.
    void Unload();

    //
    void LoadFromIDList(IShellFolder *targetFolder, PIDLIST_ABSOLUTE idList, bool dontExpandFolders);

    //
    void LoadSingleItem(IShellFolder *targetFolder, PIDLIST_RELATIVE itemID, bool dontExpandFolders);

    // True if the content needs to be reloaded every time the popup is shown.
    bool dynamic;

    // True if the content has been loaded.
    bool loaded;

    // True for content that is expensive to build and rarely changes (the Programs tree):
    // it is loaded once and kept between shows instead of being rebuilt every time.
    bool keepLoaded;

    // Set when a kept popup learns its folder changed in a way we do not patch in place
    // (delete, rename); it is then rebuilt the next time it is shown.
    bool stale;

    // If the source is a folder, the path to it.
    std::list<LPCTSTR> paths;

    // What to retrive the popup contents from.
    ContentSource source;

    // Message, SHChangeNofityRegister return value, IShellFolder
    typedef std::map<UINT, std::pair<ULONG, IShellFolder *>> WATCHFOLDERMAP;

    // Folders which this popup is watching for changes.
    WATCHFOLDERMAP watchedFolders;
};
