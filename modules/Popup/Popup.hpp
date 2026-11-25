/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *  Popup.h
 *  The nModules Project
 *
 *  Represents a popup box.
 *  
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma once

class Popup;

#include "PopupItem.hpp"
#include "PopupSettings.hpp"
#include <vector>
#include "../ModuleKit/MessageHandler.hpp"
#include "../ModuleKit/Settings.hpp"
#include "../ModuleKit/Window.hpp"

using std::vector;

class Popup : public Drawable {
private:
    enum class State
    {
        Base = 0,
        Count
    };

public:
    explicit Popup(LPCTSTR title, LPCTSTR bang, LPCTSTR prefix);
    virtual ~Popup();

public:
    void AddItem(PopupItem* item);
    void RemoveItem(PopupItem* item);
    void Show();
    void Show(int x, int y);
    void Show(LPRECT position, Popup* owner = NULL);

    // Sizes and places a visible popup again after its items changed (content that
    // finished loading after the popup was shown). Does nothing while it is hidden.
    void Refit();
    
    // Called by PopupItems
    void OpenChild(Popup*, LPRECT position, PopupItem* childItem);
    void CloseChild(bool closing = false);

    // Called by items, children, or the owner.
    virtual void Close();

    // While suspended, popups ignore losing the focus and clicks outside them. Used while
    // an item shows a system context menu, which would otherwise count as "outside" and
    // close (and free) the popup that is showing it.
    static void SuspendAutoClose(bool suspend);

    //
    LPCTSTR GetBang();

public:
    //
    LRESULT WINAPI HandleMessage(HWND, UINT, WPARAM, LPARAM, LPVOID);

public:
    //
    bool noIcons;

    //
    PopupSettings mPopupSettings;

    StateRender<State> mStateRender;

protected:
    //
    virtual void PreShow() = 0;
    virtual void PostClose() = 0;

    //
    void Size(LPRECT limits);

    // Takes items out of the popup and deletes them.
    void DeleteItem(PopupItem *item);

    //
    vector<PopupItem*> items;

private:
    //
    PopupItem* childItem;

    //
    bool CheckFocus(HWND newActive, __int8 direction);

    // Sizes the popup if needed and moves it next to "position" (the item or point it
    // opens from), inside the configured limits.
    void Place(LPRECT position);

    // Where the popup was last shown from, for Refit.
    RECT mLastPosition;

    //
    int itemSpacing;

    //
    int maxWidth;

    //
    RECT padding;

    // The bang command used to open this popup, if it's a root popup. This pointer is also stored in rootpopup.
    const LPCTSTR bang;

    // True if the popup is already sized properly.
    bool sized;

    // The currently open child, or NULL
    Popup* openChild;

    // The popup which opened this popup, or NULL
    Popup* owner;

    // True if the mouse is currently above the popup.
    bool mouseOver;

    // True to have children appear to the left rather than the right.
    bool expandLeft;

    //
    bool confineToWorkArea;

    //
    bool confineToMonitor;

    int mChildOffsetX;
    int mChildOffsetY;
};

