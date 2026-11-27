//-------------------------------------------------------------------------------------------------
// /Wallpaper/DesktopBackground.cpp
// The nModules Project
//
// The desktop's background when Explorer does not run. Explorer paints the wallpaper in
// its own desktop window (Progman); when LiteStep is the session's shell nothing does, so
// this window takes its place: it stays under every other window, paints the wallpaper
// Windows has (SPI_GETDESKWALLPAPER, the WallpaperStyle / TileWallpaper values) on each
// monitor, and runs the theme's DesktopRightClick / DesktopMiddleClick bangs.
//
// It is never created next to Explorer: re-parenting or covering Explorer's desktop is
// what broke the desktop with the old Desk.dll (see AGENTS.md).
//-------------------------------------------------------------------------------------------------
#include "../ModuleKit/LiteStep.h"
#include "../ModuleKit/LSModule.hpp"

#include "DesktopBackground.h"

#include <algorithm>
#include <objidl.h>
#include <gdiplus.h>
#include <strsafe.h>

#pragma comment(lib, "gdiplus.lib")

extern LSModule gLSModule;

namespace {
  const wchar_t kClassName[] = L"LSDesktopBackground";

  HWND sWindow = nullptr;
  HBITMAP sBitmap = nullptr;   // the rendered background, the size of the virtual screen
  ULONG_PTR sGdiplusToken = 0;

  enum class Style { Center, Tile, Stretch, Fit, Fill, Span };

  /// <summary>
  /// How Windows fits the wallpaper: HKCU\Control Panel\Desktop WallpaperStyle (0 center,
  /// 2 stretch, 6 fit, 10 fill, 22 span) and TileWallpaper (1 = tile, with style 0).
  /// </summary>
  Style ReadStyle() {
    wchar_t style[16] = L"10", tile[16] = L"0";
    DWORD size = sizeof(style);
    RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"WallpaperStyle",
      RRF_RT_REG_SZ, nullptr, style, &size);
    size = sizeof(tile);
    RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"TileWallpaper",
      RRF_RT_REG_SZ, nullptr, tile, &size);
    switch (_wtoi(style)) {
    case 0: return _wtoi(tile) == 1 ? Style::Tile : Style::Center;
    case 2: return Style::Stretch;
    case 6: return Style::Fit;
    case 22: return Style::Span;
    default: return Style::Fill;
    }
  }

  /// <summary>
  /// Draws the image into one area (a monitor, or the whole screen for Span).
  /// </summary>
  void DrawInto(Gdiplus::Graphics &graphics, Gdiplus::Image &image, Style style, const RECT &area) {
    const float areaW = float(area.right - area.left), areaH = float(area.bottom - area.top);
    const float imageW = float(image.GetWidth()), imageH = float(image.GetHeight());
    if (areaW <= 0 || areaH <= 0 || imageW <= 0 || imageH <= 0) {
      return;
    }

    Gdiplus::Region clip(Gdiplus::RectF(float(area.left), float(area.top), areaW, areaH));
    graphics.SetClip(&clip);

    switch (style) {
    case Style::Tile: {
        Gdiplus::TextureBrush brush(&image);
        brush.TranslateTransform(float(area.left), float(area.top));
        graphics.FillRectangle(&brush, float(area.left), float(area.top), areaW, areaH);
      }
      break;

    case Style::Center:
      graphics.DrawImage(&image, area.left + (areaW - imageW) / 2, area.top + (areaH - imageH) / 2,
        imageW, imageH);
      break;

    case Style::Stretch:
      graphics.DrawImage(&image, float(area.left), float(area.top), areaW, areaH);
      break;

    case Style::Fit:
    case Style::Fill:
    case Style::Span: {
        // Fit: the whole picture shows (bars on two sides). Fill/Span: the area is covered
        // (the picture is cropped).
        const float scale = style == Style::Fit
          ? std::min(areaW / imageW, areaH / imageH)
          : std::max(areaW / imageW, areaH / imageH);
        const float w = imageW * scale, h = imageH * scale;
        graphics.DrawImage(&image, area.left + (areaW - w) / 2, area.top + (areaH - h) / 2, w, h);
      }
      break;
    }

    graphics.ResetClip();
  }

  /// <summary>
  /// Renders the wallpaper for the whole virtual screen into sBitmap.
  /// </summary>
  void Render() {
    const int x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN), height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    HDC screen = GetDC(nullptr);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    HDC memory = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    HGDIOBJ old = SelectObject(memory, bitmap);

    // The desktop colour shows where the picture does not reach (Center, Fit) or when
    // there is no picture.
    RECT all = { 0, 0, width, height };
    HBRUSH background = CreateSolidBrush(GetSysColor(COLOR_DESKTOP));
    FillRect(memory, &all, background);
    DeleteObject(background);

    wchar_t path[MAX_PATH] = L"";
    SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, path, 0);
    if (path[0] != L'\0') {
      Gdiplus::Image image(path);
      if (image.GetLastStatus() == Gdiplus::Ok) {
        Gdiplus::Graphics graphics(memory);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        const Style style = ReadStyle();
        if (style == Style::Span) {
          DrawInto(graphics, image, style, all);
        } else {
          struct Context { Gdiplus::Graphics *graphics; Gdiplus::Image *image; Style style; int x, y; };
          Context context = { &graphics, &image, style, x, y };
          EnumDisplayMonitors(nullptr, nullptr, [] (HMONITOR, HDC, LPRECT rect, LPARAM data) -> BOOL {
            const Context &c = *reinterpret_cast<Context*>(data);
            RECT area = { rect->left - c.x, rect->top - c.y, rect->right - c.x, rect->bottom - c.y };
            DrawInto(*c.graphics, *c.image, c.style, area);
            return TRUE;
          }, LPARAM(&context));
        }
      }
    }

    SelectObject(memory, old);
    DeleteDC(memory);

    if (sBitmap != nullptr) {
      DeleteObject(sBitmap);
    }
    sBitmap = bitmap;

    if (sWindow != nullptr) {
      SetWindowPos(sWindow, HWND_BOTTOM, x, y, width, height, SWP_NOACTIVATE);
      InvalidateRect(sWindow, nullptr, FALSE);
    }
  }

  /// <summary>
  /// Runs the bang of a theme's desktop click setting (DesktopRightClick, DesktopMiddleClick).
  /// </summary>
  void RunDesktopClick(LPCWSTR setting) {
    wchar_t command[MAX_LINE_LENGTH] = L"";
    if (LiteStep::GetRCLine(setting, command, _countof(command), L"") && command[0] != L'\0') {
      LiteStep::LSExecute(sWindow, command, SW_SHOWNORMAL);
    }
  }

  LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        if (sBitmap != nullptr) {
          HDC memory = CreateCompatibleDC(dc);
          HGDIOBJ old = SelectObject(memory, sBitmap);
          BitBlt(dc, paint.rcPaint.left, paint.rcPaint.top,
            paint.rcPaint.right - paint.rcPaint.left, paint.rcPaint.bottom - paint.rcPaint.top,
            memory, paint.rcPaint.left, paint.rcPaint.top, SRCCOPY);
          SelectObject(memory, old);
          DeleteDC(memory);
        }
        EndPaint(window, &paint);
      }
      return 0;

    case WM_ERASEBKGND:
      return 1;

    case WM_WINDOWPOSCHANGING:
      // Under every other window, whatever asks to raise it.
      LPWINDOWPOS(lParam)->hwndInsertAfter = HWND_BOTTOM;
      LPWINDOWPOS(lParam)->flags &= ~SWP_NOZORDER;
      return 0;

    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;

    case WM_SETTINGCHANGE:
      if (wParam == SPI_SETDESKWALLPAPER || wParam == SPI_SETWORKAREA ||
          (lParam != 0 && wcscmp(LPCWSTR(lParam), L"WallpaperStyle") == 0)) {
        Render();
      }
      return 0;

    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
      Render();
      return 0;

    case WM_SYSCOLORCHANGE:
      Render();
      return 0;

    case WM_RBUTTONUP:
      RunDesktopClick(L"DesktopRightClick");
      return 0;

    case WM_MBUTTONUP:
      RunDesktopClick(L"DesktopMiddleClick");
      return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
  }
}


/// <summary>
/// True if Explorer's desktop exists: a top-level window (Progman, or a WorkerW) holding
/// SHELLDLL_DefView, the view that paints the wallpaper and the icons. A Progman window
/// alone does not count: ShellAppRuntime owns the shell window without painting a desktop.
/// </summary>
static bool ExplorerDesktopExists() {
  bool found = false;
  EnumWindows([] (HWND window, LPARAM data) -> BOOL {
    if (FindWindowExW(window, nullptr, L"SHELLDLL_DefView", nullptr) != nullptr) {
      *reinterpret_cast<bool*>(data) = true;
      return FALSE;
    }
    return TRUE;
  }, LPARAM(&found));
  return found;
}


void DesktopBackground::Create() {
  // Explorer paints its own desktop: leave it alone.
  if (sWindow != nullptr || ExplorerDesktopExists()) {
    return;
  }

  Gdiplus::GdiplusStartupInput input;
  if (Gdiplus::GdiplusStartup(&sGdiplusToken, &input, nullptr) != Gdiplus::Ok) {
    sGdiplusToken = 0;
    return;
  }

  WNDCLASSEXW windowClass = { sizeof(windowClass) };
  windowClass.lpfnWndProc = WindowProc;
  windowClass.hInstance = gLSModule.GetInstance();
  windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
  windowClass.lpszClassName = kClassName;
  RegisterClassExW(&windowClass);

  sWindow = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClassName, L"", WS_POPUP,
    GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
    GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN),
    nullptr, nullptr, gLSModule.GetInstance(), nullptr);
  if (sWindow == nullptr) {
    return;
  }
  // VWM and the task lists leave LiteStep's own windows alone (see litestep.cpp).
  SetWindowLongPtrW(sWindow, GWLP_USERDATA, magicDWord);

  Render();
  ShowWindow(sWindow, SW_SHOWNOACTIVATE);
}


void DesktopBackground::Destroy() {
  if (sWindow != nullptr) {
    DestroyWindow(sWindow);
    sWindow = nullptr;
    UnregisterClassW(kClassName, gLSModule.GetInstance());
  }
  if (sBitmap != nullptr) {
    DeleteObject(sBitmap);
    sBitmap = nullptr;
  }
  if (sGdiplusToken != 0) {
    Gdiplus::GdiplusShutdown(sGdiplusToken);
    sGdiplusToken = 0;
  }
}
