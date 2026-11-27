//-------------------------------------------------------------------------------------------------
// /TaskBar/MinimizedShelf.cpp
// The nModules Project
//
// See MinimizedShelf.hpp.
//-------------------------------------------------------------------------------------------------
#include "../ModuleKit/LiteStep.h"
#include "../ModuleKit/LSModule.hpp"
#include "../Utilities/WindowActivation.h"

#include "MinimizedShelf.hpp"
#include "Taskbar.hpp"
#include "WindowManager.h"

#include <algorithm>
#include <windowsx.h>

#pragma comment(lib, "dwmapi.lib")

extern LSModule gLSModule;

std::unique_ptr<MinimizedShelf> gMinimizedShelf;

namespace {
  // nVWM marks the windows it has parked on another desktop.
  const WCHAR kParkedProp[] = L"LSVWMDesk";

  const WindowSettings sShelfDefaults([] (WindowSettings &defaults) {
    // On the desktop, under the windows, like desktop icons.
    defaults.onDesktop = true;
    defaults.alwaysOnTop = false;
  });

  const StateRender<MinimizedShelf::States>::InitData sInitData(
      [] (StateRender<MinimizedShelf::States>::InitData &initData) {
    initData[MinimizedShelf::States::Base].defaults.brushSettings[::State::BrushType::Background].color =
      Color::Create(0x00000000);
  });
}


MinimizedShelf::MinimizedShelf()
    : Drawable(L"MinimizedShelf") {
  mStateRender.Load(sInitData, mSettings);
  mThumbHeight = std::max(16, mSettings->GetInt(L"ThumbHeight", 110));
  mMaxThumbWidth = std::max(mThumbHeight / 2, mSettings->GetInt(L"MaxThumbWidth", 200));
  mSpacing = mSettings->GetInt(L"Spacing", 12);
  mPadding = mSettings->GetInt(L"Padding", 8);
  mMarginLeft = mSettings->GetInt(L"MarginLeft", 12);
  mMarginBottom = mSettings->GetInt(L"MarginBottom", 12);

  WindowSettings windowSettings;
  windowSettings.Load(mSettings, &sShelfDefaults);
  windowSettings.hidden = true;
  mWindow->Initialize(windowSettings, &mStateRender);
}


MinimizedShelf::~MinimizedShelf() {
  for (Item &item : mItems) {
    DwmUnregisterThumbnail(item.thumbnail);
  }
  mItems.clear();
}


void MinimizedShelf::Start() {
  Refresh();
}


bool MinimizedShelf::Accepts(HWND window) const {
  return IsIconic(window) && GetPropW(window, kParkedProp) == nullptr &&
    WindowManager::IsTaskbarWindow(window);
}


int MinimizedShelf::WidthFor(HWND window, HTHUMBNAIL thumbnail) const {
  // The window's normal (restored) shape: a minimized window's own rectangle is tiny.
  float aspect = 4.0f / 3.0f;
  WINDOWPLACEMENT placement = { sizeof(placement) };
  if (GetWindowPlacement(window, &placement)) {
    const RECT &r = placement.rcNormalPosition;
    if (r.right > r.left && r.bottom > r.top) {
      aspect = float(r.right - r.left) / float(r.bottom - r.top);
    }
  }
  SIZE source = { 0, 0 };
  if (thumbnail != nullptr && SUCCEEDED(DwmQueryThumbnailSourceSize(thumbnail, &source)) &&
      source.cx > 0 && source.cy > 0) {
    aspect = float(source.cx) / float(source.cy);
  }
  return std::min(mMaxThumbWidth, std::max(mThumbHeight / 2, int(mThumbHeight * aspect + 0.5f)));
}


void MinimizedShelf::ComputeLayout(const std::vector<int> &widths, RECT &row, std::vector<RECT> &slots) const {
  RECT area;
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);

  int x = mPadding;
  slots.clear();
  for (int width : widths) {
    slots.push_back({ x, mPadding, x + width, mPadding + mThumbHeight });
    x += width + mSpacing;
  }
  const int rowWidth = widths.empty() ? 0 : x - mSpacing + mPadding;
  const int rowHeight = mThumbHeight + 2 * mPadding;
  row.left = area.left + mMarginLeft;
  row.bottom = area.bottom - mMarginBottom;
  row.top = row.bottom - rowHeight;
  row.right = row.left + rowWidth;
}


void MinimizedShelf::Layout() {
  if (mItems.empty()) {
    mWindow->Hide();
    return;
  }

  std::vector<int> widths;
  for (const Item &item : mItems) {
    widths.push_back(item.width);
  }
  RECT row;
  std::vector<RECT> slots;
  ComputeLayout(widths, row, slots);

  mWindow->SetPosition(float(row.left), float(row.top), float(row.right - row.left), float(row.bottom - row.top));
  for (size_t i = 0; i < mItems.size(); ++i) {
    DWM_THUMBNAIL_PROPERTIES properties = {};
    properties.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
    properties.rcDestination = slots[i];
    properties.fVisible = TRUE;
    properties.opacity = 255;
    properties.fSourceClientAreaOnly = FALSE;
    DwmUpdateThumbnailProperties(mItems[i].thumbnail, &properties);
  }
  if (!mWindow->IsVisible()) {
    mWindow->Show();
  }
}


void MinimizedShelf::Add(HWND window) {
  for (const Item &item : mItems) {
    if (item.window == window) {
      return;
    }
  }
  if (!Accepts(window)) {
    return;
  }
  HTHUMBNAIL thumbnail = nullptr;
  if (FAILED(DwmRegisterThumbnail(mWindow->GetWindowHandle(), window, &thumbnail))) {
    return;
  }
  mItems.push_back({ window, thumbnail, WidthFor(window, thumbnail) });
  Layout();
}


void MinimizedShelf::Remove(HWND window) {
  for (auto item = mItems.begin(); item != mItems.end(); ++item) {
    if (item->window == window) {
      DwmUnregisterThumbnail(item->thumbnail);
      mItems.erase(item);
      Layout();
      return;
    }
  }
}


void MinimizedShelf::Refresh() {
  bool changed = false;
  for (auto item = mItems.begin(); item != mItems.end();) {
    if (!IsWindow(item->window) || !Accepts(item->window)) {
      DwmUnregisterThumbnail(item->thumbnail);
      item = mItems.erase(item);
      changed = true;
    } else {
      ++item;
    }
  }

  // Minimized windows we have not seen (minimized before LiteStep started, or while a
  // window was parked on another desktop).
  std::vector<HWND> found;
  EnumWindows([] (HWND window, LPARAM data) -> BOOL {
    if (IsIconic(window)) {
      reinterpret_cast<std::vector<HWND>*>(data)->push_back(window);
    }
    return TRUE;
  }, LPARAM(&found));
  for (HWND window : found) {
    bool known = std::any_of(mItems.begin(), mItems.end(), [window] (const Item &item) { return item.window == window; });
    HTHUMBNAIL thumbnail = nullptr;
    if (!known && Accepts(window) &&
        SUCCEEDED(DwmRegisterThumbnail(mWindow->GetWindowHandle(), window, &thumbnail))) {
      mItems.push_back({ window, thumbnail, WidthFor(window, thumbnail) });
      changed = true;
    }
  }

  if (changed) {
    Layout();
  }
}


bool MinimizedShelf::GetSlot(HWND window, LPRECT rect) {
  std::vector<int> widths;
  size_t index = mItems.size();
  for (size_t i = 0; i < mItems.size(); ++i) {
    widths.push_back(mItems[i].width);
    if (mItems[i].window == window) {
      index = i;
    }
  }
  if (index == mItems.size()) {
    // Being minimized now: it will be the next one in the row.
    if (IsIconic(window) || !WindowManager::IsTaskbarWindow(window) || GetPropW(window, kParkedProp) != nullptr) {
      return false;
    }
    widths.push_back(WidthFor(window, nullptr));
  }
  RECT row;
  std::vector<RECT> slots;
  ComputeLayout(widths, row, slots);
  *rect = slots[index];
  OffsetRect(rect, row.left, row.top);
  return true;
}


LRESULT WINAPI MinimizedShelf::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam, LPVOID) {
  if (message == WM_LBUTTONUP) {
    POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    std::vector<int> widths;
    for (const Item &item : mItems) {
      widths.push_back(item.width);
    }
    RECT row;
    std::vector<RECT> slots;
    ComputeLayout(widths, row, slots);
    for (size_t i = 0; i < slots.size(); ++i) {
      if (PtInRect(&slots[i], point)) {
        HWND target = mItems[i].window;
        ShowWindowAsync(target, SW_RESTORE);
        WindowActivation::ForceForeground(target);
        break;
      }
    }
  }

  mEventHandler->HandleMessage(window, message, wParam, lParam);
  return DefWindowProc(window, message, wParam, lParam);
}
