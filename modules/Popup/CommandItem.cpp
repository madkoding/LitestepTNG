/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *  CommandItem.cpp
 *  The nModules Project
 *
 *  Represents a popup item which executes a command.
 *  
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#include "../ModuleKit/LiteStep.h"
#include <strsafe.h>
#include "CommandItem.hpp"
#include "../ModuleKit/LSModule.hpp"
#include "Popup.hpp"
#include "ContentPopup.hpp"
#include <Shlwapi.h>


CommandItem::CommandItem(LPCTSTR title, LPCTSTR command, Drawable* parent)
    : PopupItem(parent, L"Item", PopupItem::Type::Command)
    , title(_wcsdup(title))
    , command(_wcsdup(command))
{
    mWindow->Initialize(((Popup*)mParent)->mPopupSettings.mCommandWindowSettings, &((Popup*)mParent)->mPopupSettings.mCommandStateRender);
    mWindow->SetText(title);
}


CommandItem::CommandItem(Drawable* parent, LPCTSTR title, LPCTSTR command, LPCTSTR customIcon)
    : CommandItem(title, command, parent)
{
    ParseDotIcon(customIcon);
    mWindow->Show();
}


CommandItem::CommandItem(Drawable* parent, LPCTSTR title, LPCTSTR command, HICON icon)
    : CommandItem(title, command, parent)
{
    if (icon != nullptr)
    {
        AddIcon(icon);
    }
    mWindow->Show();
}


CommandItem::~CommandItem() {
    free((LPVOID)this->title);
    free((LPVOID)this->command);
}


int CommandItem::GetDesiredWidth(int maxWidth) {
    SIZE s;
    mWindow->GetDesiredSize(maxWidth, (int)mWindow->GetSize().height, &s);
    return s.cx;
}


LRESULT CommandItem::HandleMessage(HWND window, UINT msg, WPARAM wParam, LPARAM lParam, LPVOID) {
    switch (msg) {
    case WM_LBUTTONDOWN:
        {
            LiteStep::LSExecute(NULL, this->command, SW_SHOW);
            ((Popup*)mParent)->Close();
        }
        return 0;

    case WM_RBUTTONDOWN:
        return 0;

    case WM_RBUTTONUP:
        // May close the popup, which can delete this item: nothing may follow it.
        ShowContextMenu();
        return 0;

    case WM_MOUSEMOVE:
        {
            ((Popup*)mParent)->mPopupSettings.mCommandStateRender.ActivateState(State::Hover, mWindow);
            ((Popup*)mParent)->CloseChild();
        }
        return 0;

    case WM_MOUSELEAVE:
        {
            ((Popup*)mParent)->mPopupSettings.mCommandStateRender.ClearState(State::Hover, mWindow);
        }
        return 0;
    }
    return DefWindowProc(window, msg, wParam, lParam);
}



/// <summary>
/// Shows the right-click menu of an item that stands for something that can be a
/// favorite: a file (a shortcut from the Programs tree, a recent document, a favorite)
/// or an application from the "all applications" list. Items that run a bang have none.
/// </summary>
void CommandItem::ShowContextMenu() {
  // Either a quoted file path, or the command that launches an Applications-folder entry.
  WCHAR file[MAX_PATH] = L"", app[1024] = L"";
  size_t prefixLength = wcslen(Favorites::AppCommandPrefix);
  if (_wcsnicmp(this->command, Favorites::AppCommandPrefix, prefixLength) == 0) {
    StringCchCopyW(app, _countof(app), this->command + prefixLength);
    size_t length = wcslen(app);
    if (length > 0 && app[length - 1] == L'"') {
      app[length - 1] = 0;
    }
    if (app[0] == 0) {
      return;
    }
  } else {
    if (FAILED(StringCchCopyW(file, _countof(file), this->command))) {
      return;
    }
    PathUnquoteSpacesW(file);
    if (!PathFileExistsW(file) || PathIsDirectoryW(file)) {
      return;
    }
  }

  const bool isFavorite = file[0] != 0 && Favorites::Contains(file);
  WCHAR text[128];
  LiteStep::GetRCString(isFavorite ? L"nPopupUnpinText" : L"nPopupPinText", text,
    isFavorite ? L"Remove from favorites" : L"Add to favorites", _countof(text));

  HMENU menu = CreatePopupMenu();
  if (menu == nullptr) {
    return;
  }
  AppendMenuW(menu, MF_STRING, 1, text);

  Popup *popup = (Popup*)mParent;
  HWND owner = popup->GetWindow()->GetWindowHandle();
  POINT cursor;
  GetCursorPos(&cursor);

  // The system menu is not one of our popups: without this, clicking it would count as
  // a click outside and close the popup under us.
  Popup::SuspendAutoClose(true);
  SetForegroundWindow(owner);
  int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
    cursor.x, cursor.y, 0, owner, nullptr);
  Popup::SuspendAutoClose(false);
  DestroyMenu(menu);

  if (choice != 1) {
    return;
  }

  if (isFavorite) {
    // The list changed, so close it; it is rebuilt the next time it opens.
    if (Favorites::Remove(file)) {
      popup->Close();
    }
  } else if (app[0] != 0) {
    Favorites::AddApp(app, this->title);
  } else {
    Favorites::Add(file);
  }
}
