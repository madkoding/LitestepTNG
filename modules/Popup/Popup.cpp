/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *  Popup.cpp
 *  The nModules Project
 *
 *  Represents a popup box.
 *  
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#include "../ModuleKit/LiteStep.h"
#include <strsafe.h>
#include "Popup.hpp"
#include "../ModuleKit/LSModule.hpp"
#include "../ModuleKit/MonitorInfo.hpp"
#include "FolderItem.hpp"
#include "../Utilities/Math.h"
#include "../CoreCom/Core.h"
#include <algorithm>
#include <unordered_set>


// --- Closing on a click outside ------------------------------------------------
// A menu must go away when the user clicks anywhere else. Waiting for WM_ACTIVATE is
// not enough: when LiteStep runs on top of Explorer a popup is not always the active
// window, and then it never hears about the click. So while a root popup is open, a
// low-level mouse hook watches for button presses that land outside every popup window
// of this module and asks the root to close. The click itself is not touched.

// Every popup window of this module; a press on any of them is "inside".
static std::unordered_set<HWND> sPopupWindows;
// Window of the root popup that is currently open, or nullptr.
static HWND sOpenRootWindow = nullptr;
static HHOOK sOutsideClickHook = nullptr;
// lParam of the WM_ACTIVATE we post for an outside click. A real deactivation carries a
// window handle (or 0) there, never this value.
static const LPARAM kOutsideClick = -1;
// See Popup::SuspendAutoClose.
static bool sAutoCloseSuspended = false;

void Popup::SuspendAutoClose(bool suspend) {
  sAutoCloseSuspended = suspend;
}

static LRESULT CALLBACK OutsideClickProc(int code, WPARAM wParam, LPARAM lParam) {
  if (code == HC_ACTION && sOpenRootWindow != nullptr && !sAutoCloseSuspended &&
      (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN || wParam == WM_MBUTTONDOWN)) {
    const MSLLHOOKSTRUCT *info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
    HWND target = GetAncestor(WindowFromPoint(info->pt), GA_ROOT);
    if (sPopupWindows.find(target) == sPopupWindows.end()) {
      // Same path as losing activation, handled on the module's thread, not in the hook.
      PostMessage(sOpenRootWindow, WM_ACTIVATE, WA_INACTIVE, kOutsideClick);
    }
  }
  return CallNextHookEx(sOutsideClickHook, code, wParam, lParam);
}

static void WatchOutsideClicks(HWND rootWindow) {
  sOpenRootWindow = rootWindow;
  if (sOutsideClickHook == nullptr) {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
      (LPCWSTR)&OutsideClickProc, &module);
    sOutsideClickHook = SetWindowsHookExW(WH_MOUSE_LL, OutsideClickProc, module, 0);
  }
}

static void StopWatchingOutsideClicks(HWND rootWindow) {
  if (sOpenRootWindow == rootWindow) {
    sOpenRootWindow = nullptr;
    if (sOutsideClickHook != nullptr) {
      UnhookWindowsHookEx(sOutsideClickHook);
      sOutsideClickHook = nullptr;
    }
  }
}


/// <summary>
/// Makes the window the foreground window. Windows only grants that to the process that
/// received the last input; when it refuses (a popup opened by a hotkey or a bang rather
/// than by a click on one of our windows) we borrow the right from the thread that
/// currently owns the foreground by attaching to its input queue for a moment.
/// </summary>
static void TakeForeground(HWND window) {
  SetForegroundWindow(window);
  HWND foreground = GetForegroundWindow();
  if (foreground == window || foreground == nullptr) {
    return;
  }
  DWORD self = GetCurrentThreadId();
  DWORD other = GetWindowThreadProcessId(foreground, nullptr);
  if (other != 0 && other != self && AttachThreadInput(self, other, TRUE)) {
    SetForegroundWindow(window);
    AttachThreadInput(self, other, FALSE);
  }
}


Popup::Popup(LPCTSTR title, LPCTSTR bang, LPCTSTR prefix)
    : Drawable(prefix)
    , bang(bang != nullptr ? _wcsdup(bang) : nullptr)
    , openChild(nullptr)
    , owner(nullptr)
{
    this->itemSpacing = mSettings->GetInt(L"ItemSpacing", 2);
    this->maxWidth = mSettings->GetInt(L"MaxWidth", 300);
    this->noIcons = mSettings->GetBool(L"NoIcons", false);
    this->expandLeft = mSettings->GetBool(L"ExpandLeft", false);
    this->confineToMonitor = mSettings->GetBool(L"ConfineToMonitor", false);
    this->confineToWorkArea = mSettings->GetBool(L"ConfineToWorkArea", false);
    mChildOffsetX = mSettings->GetInt(L"ChildOffsetX", 0);
    mChildOffsetY = mSettings->GetInt(L"ChildOffsetY", 0);
    this->padding = mSettings->GetOffsetRect(L"Padding", 5, 5, 5, 5);

    mPopupSettings.Load(mSettings);

    WindowSettings defaultSettings, windowSettings;
    defaultSettings.alwaysOnTop = true;
    defaultSettings.width = 200;
    windowSettings.Load(mSettings, &defaultSettings);

    StateRender<State>::InitData initData;
    initData[State::Base].defaults.brushSettings[::State::BrushType::Background].color = Color::Create(0x440000FF);
    initData[State::Base].defaults.fontSize = 32.0f;
    initData[State::Base].defaults.textAlign = DWRITE_TEXT_ALIGNMENT_CENTER;
    initData[State::Base].defaults.textVerticalAlign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
    mStateRender.Load(initData, mSettings);

    mWindow->Initialize(windowSettings, &mStateRender);
    mWindow->SetText(title);

    SetParent(mWindow->GetWindowHandle(), NULL);
    sPopupWindows.insert(mWindow->GetWindowHandle());
    SetWindowLongPtr(mWindow->GetWindowHandle(), GWL_EXSTYLE, GetWindowLongPtr(mWindow->GetWindowHandle(), GWL_EXSTYLE) & ~WS_EX_NOACTIVATE);
    this->sized = false;
    this->mouseOver = false;
    this->childItem = NULL;
}


Popup::~Popup()
{
    StopWatchingOutsideClicks(mWindow->GetWindowHandle());
    sPopupWindows.erase(mWindow->GetWindowHandle());
    for (PopupItem * item : this->items)
    {
        delete item;
    }
    if (this->bang != nullptr)
    {
        free((LPVOID)this->bang);
    }
}


void Popup::AddItem(PopupItem* item)
{
    this->items.push_back(item);
    this->sized = false;
    if (mWindow->IsVisible())
    {
        /*MonitorInfo* monInfo = mWindow->GetMonitorInformation();
        RECT limits = monInfo->m_virtualDesktop.rect;

        if (this->confineToMonitor && this->confineToWorkArea) {
            limits = monInfo->m_monitors.at(monInfo->MonitorFromRECT(position)).workArea;
        }
        else if (this->confineToMonitor) {
            limits = monInfo->m_monitors.at(monInfo->MonitorFromRECT(position)).rect;
        }
        else if (this->confineToWorkArea) {
            limits = monInfo->m_monitors.at(monInfo->MonitorFromRECT(position)).workArea;
        }

        Size();*/
        mWindow->Repaint();
    }
}


void Popup::RemoveItem(PopupItem* /* item */)
{
}


void Popup::CloseChild(bool closing)
{
    if (this->openChild != nullptr)
    {
        if (!closing)
        {
            SetFocus(mWindow->GetWindowHandle());
            SetActiveWindow(mWindow->GetWindowHandle());
        }

        this->openChild->owner = nullptr;
        ((nPopup::FolderItem*)this->childItem)->ClosingPopup();
        this->openChild->Close();
        this->childItem = nullptr;
        this->openChild = nullptr;
    }
}


void Popup::OpenChild(Popup* child, LPRECT position, PopupItem* childItem)
{
    if (child != this->openChild)
    {
        CloseChild();
        this->openChild = child;
        this->childItem = childItem;
        this->openChild->expandLeft = this->expandLeft;
        position->top -= this->padding.top + mChildOffsetY;
        position->bottom -= this->padding.top;
        position->left -= this->padding.left + mChildOffsetX;
        position->right += this->padding.right + mChildOffsetX;
        this->openChild->Show(position, this);
    }
}


LPCTSTR Popup::GetBang()
{
    return this->bang;
}


bool Popup::CheckFocus(HWND newActive, __int8 direction)
{
    if (mWindow->GetWindowHandle() == newActive || this->mouseOver)
        return true;
    return direction & 1 && this->owner && this->owner->CheckFocus(newActive, 1)
        || direction & 2 && this->openChild && this->openChild->CheckFocus(newActive, 2);
}


void Popup::Close()
{
    StopWatchingOutsideClicks(mWindow->GetWindowHandle());
    mWindow->Hide();
    this->expandLeft = mSettings->GetBool(L"ExpandLeft", false);
    CloseChild(true);
    //PostClose();
    this->mouseOver = false;
    if (this->owner != nullptr)
    {
        this->owner->openChild = nullptr;
        ((nPopup::FolderItem*)this->owner->childItem)->ClosingPopup();
        this->owner->childItem = nullptr;
        Popup* owner = this->owner;
        this->owner = nullptr;
        owner->Close();
    }
}


void Popup::Show()
{
    POINT pt;
    GetCursorPos(&pt);
    Show(pt.x, pt.y);
}


void Popup::Show(int x, int y)
{
    RECT r;
    r.left = x - 1; r.right = x + 1;
    r.top = y - 1; r.bottom = y + 1;
    Show(&r);
}


void Popup::Size(LPRECT limits)
{
    // Work out the desired item 
    int itemWidth = mSettings->GetInt(L"Width", 200) - this->padding.left - this->padding.right;
    int maxItemWidth = this->maxWidth - this->padding.left - this->padding.right;
    int height = this->padding.top;
    int width;
    for (PopupItem *item : this->items)
    {
        item->Position(this->padding.left, height);
        height += item->GetHeight() + this->itemSpacing;
        itemWidth = std::max(itemWidth, item->GetDesiredWidth(maxItemWidth));
    }
    width = itemWidth + this->padding.left + this->padding.right;
    height += this->padding.bottom - this->itemSpacing;

    // We've excceeded the max height, split the popup into columns.
    if (height > limits->bottom - limits->top)
    {
        int columns = (height - this->padding.top - this->padding.bottom)/(limits->bottom - limits->top - this->padding.top - this->padding.bottom) + 1;
        int columnWidth = width;
        width = columnWidth * columns + this->itemSpacing*(columns - 1);
        height = this->padding.top;
        int column = 0;
        int rowHeight = 0;
        for (PopupItem *item : this->items)
        {
            item->Position(this->padding.left + (columnWidth + this->itemSpacing) * column, height);
            rowHeight = std::max(item->GetHeight() + this->itemSpacing, rowHeight);
            column++;
            if (column == columns)
            {
                height += rowHeight;
                rowHeight = 0;
                column = 0;
            }
        }
        if (column != 0)
        {
            height += rowHeight;
        }
        height += this->padding.bottom - this->itemSpacing;
    }

    // Size all items properly
    for (PopupItem *item : this->items)
    {
        item->SetWidth(itemWidth);
    }

    // Size the main window
    mWindow->Resize((float)width, (float)height);
    this->sized = true;
}


void Popup::Show(LPRECT position, Popup* owner)
{
    this->owner = owner;
    SetParent(mWindow->GetWindowHandle(), nullptr);
    PreShow();

    mLastPosition = *position;
    Place(position);

    mWindow->Show();
    SetWindowPos(mWindow->GetWindowHandle(), HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    // Another of the shell's windows holding the mouse (the desktop icons start a selection
    // on the press that opens the menu) would get the first click meant for an item.
    HWND capture = GetCapture();
    if (capture != nullptr && sPopupWindows.find(capture) == sPopupWindows.end())
    {
        ReleaseCapture();
    }
    // When LiteStep runs on top of Explorer it is not the foreground process, and a popup
    // that is only active inside our own thread never receives WA_INACTIVE: it would stay
    // open until an item is clicked. Taking the foreground lets a click elsewhere close it.
    TakeForeground(mWindow->GetWindowHandle());
    if (owner == nullptr)
    {
        WatchOutsideClicks(mWindow->GetWindowHandle());
    }
    SetFocus(mWindow->GetWindowHandle());
    SetActiveWindow(mWindow->GetWindowHandle());
}


/// <summary>
/// Sizes the popup if needed and moves it next to position, inside the limits.
/// </summary>
void Popup::Place(LPRECT position)
{
    MonitorInfo &monInfo = nCore::FetchMonitorInfo();
    int x, y;
    RECT limits = monInfo.GetVirtualDesktop().rect;

    if (this->confineToMonitor && this->confineToWorkArea) {
      limits = monInfo.GetMonitor(monInfo.MonitorFromRECT(*position)).workArea;
    } else if (this->confineToMonitor) {
      limits = monInfo.GetMonitor(monInfo.MonitorFromRECT(*position)).rect;
    } else if (this->confineToWorkArea) {
      // TODO::Shouldn't confine to the monitor here.
      limits = monInfo.GetMonitor(monInfo.MonitorFromRECT(*position)).workArea;
    }

    if (!this->sized) {
        Size(&limits);
    }

    if (this->expandLeft)
    {
        x = position->left - (int)mWindow->GetSize().width;
        if (x < limits.left)
        {
            this->expandLeft = false;
            x = this->owner ? position->right : limits.left;
        }
    }
    else
    {
        x = position->right;
        if (x > limits.right - mWindow->GetSize().width)
        {
            this->expandLeft = true;
            x = (this->owner ? position->left : limits.right) - int(mWindow->GetSize().width + 0.5f);
        }
    }

    y = std::max<int>(limits.top, std::min<int>(limits.bottom - int(mWindow->GetSize().height + 0.5f), position->top));

    mWindow->Move((float)x, (float)y);
}


/// <summary>
/// Sizes and places a visible popup again after its items changed.
/// </summary>
void Popup::Refit()
{
    if (!mWindow->IsVisible())
    {
        return;
    }
    this->sized = false;
    Place(&mLastPosition);
    mWindow->Repaint();
}


/// <summary>
/// Takes an item out of the popup and deletes it.
/// </summary>
void Popup::DeleteItem(PopupItem *item)
{
    auto it = std::find(this->items.begin(), this->items.end(), item);
    if (it != this->items.end())
    {
        this->items.erase(it);
    }
    delete item;
    this->sized = false;
}


LRESULT Popup::HandleMessage(HWND window, UINT msg, WPARAM wParam, LPARAM lParam, LPVOID)
{
    switch (msg)
    {
    case Window::WM_HIDDEN:
        {
            PostClose();
        }
        return 0;

    case WM_ACTIVATE:
        {
            if (LOWORD(wParam) == WA_INACTIVE && mWindow->IsVisible() && !sAutoCloseSuspended)
            {
                // An outside click was already checked against every popup window; a real
                // deactivation still has to make sure focus did not just move to a submenu.
                if (lParam == kOutsideClick || !CheckFocus((HWND)lParam, 3))
                {
                    Close();
                }
            }
        }
        return 0;

    case WM_MOUSEMOVE:
        this->mouseOver = true;
        return 0;

    case WM_MOUSELEAVE:
        this->mouseOver = false;
        return 0;

    default:
        return DefWindowProc(window, msg, wParam, lParam);
    }
}

