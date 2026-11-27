//-------------------------------------------------------------------------------------------------
// /ModuleKit/ShadowWindow.cpp
// The nModules Project
//
// Drop shadows of top-level windows; see ShadowWindow.hpp.
//
// Each layer is the window's rounded shape, grown by Spread, moved by Offset, blurred with a
// gaussian of sigma Blur / 2 (three box blurs), in Color at Opacity. Layers are stacked,
// and the shape itself is cut out of the result, as CSS does with box-shadow, so a
// translucent window does not show its own shadow through itself.
//-------------------------------------------------------------------------------------------------
#include "LiteStep.h"
#include "ShadowWindow.hpp"

#include <algorithm>
#include <cmath>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {
  const wchar_t kClassName[] = L"LSShadow";

  HINSTANCE ThisModule() {
    return reinterpret_cast<HINSTANCE>(&__ImageBase);
  }

  void EnsureClass() {
    WNDCLASSEXW existing = { sizeof(existing) };
    if (GetClassInfoExW(ThisModule(), kClassName, &existing)) {
      return;
    }
    WNDCLASSEXW windowClass = { sizeof(windowClass) };
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = ThisModule();
    windowClass.lpszClassName = kClassName;
    RegisterClassExW(&windowClass);
  }

  bool SameLayers(const std::vector<State::ShadowLayer> &a, const std::vector<State::ShadowLayer> &b) {
    if (a.size() != b.size()) {
      return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
      const State::ShadowLayer &x = a[i], &y = b[i];
      if (x.enabled != y.enabled || x.offsetX != y.offsetX || x.offsetY != y.offsetY ||
          x.blurRadius != y.blurRadius || x.spread != y.spread || x.opacity != y.opacity ||
          x.color.r != y.color.r || x.color.g != y.color.g || x.color.b != y.color.b ||
          x.color.a != y.color.a) {
        return false;
      }
    }
    return true;
  }

  bool SameRadii(const State::CornerRadii &a, const State::CornerRadii &b) {
    return a.topLeft.x == b.topLeft.x && a.topRight.x == b.topRight.x &&
      a.bottomRight.x == b.bottomRight.x && a.bottomLeft.x == b.bottomLeft.x;
  }

  /// <summary>
  /// How much of the pixel at (x, y) (its centre) lies inside the rounded rectangle
  /// [left, top, right, bottom] with per-corner radii: 0..1, antialiased over one pixel.
  /// </summary>
  float Coverage(float x, float y, float left, float top, float right, float bottom,
      const State::CornerRadii &radii) {
    const float px = x + 0.5f, py = y + 0.5f;
    const float cx = (left + right) / 2, cy = (top + bottom) / 2;
    const float hw = (right - left) / 2, hh = (bottom - top) / 2;
    if (hw <= 0 || hh <= 0) {
      return 0;
    }
    float r = px < cx
      ? (py < cy ? radii.topLeft.x : radii.bottomLeft.x)
      : (py < cy ? radii.topRight.x : radii.bottomRight.x);
    r = std::max(0.0f, std::min(r, std::min(hw, hh)));
    // Signed distance to the rounded rectangle.
    const float qx = std::fabs(px - cx) - hw + r, qy = std::fabs(py - cy) - hh + r;
    const float outside = std::sqrt(std::max(qx, 0.0f) * std::max(qx, 0.0f) + std::max(qy, 0.0f) * std::max(qy, 0.0f));
    const float d = outside + std::min(std::max(qx, qy), 0.0f) - r;
    return std::max(0.0f, std::min(1.0f, 0.5f - d));
  }

  /// <summary>
  /// One horizontal and one vertical box blur of the given radius, in place.
  /// </summary>
  void BoxBlur(std::vector<float> &data, std::vector<float> &scratch, int width, int height, int radius) {
    if (radius <= 0) {
      return;
    }
    const float scale = 1.0f / float(radius * 2 + 1);
    // Horizontal: data -> scratch.
    for (int y = 0; y < height; ++y) {
      const float *row = &data[size_t(y) * width];
      float *out = &scratch[size_t(y) * width];
      float sum = 0;
      for (int x = -radius; x <= radius; ++x) {
        sum += row[std::min(std::max(x, 0), width - 1)];
      }
      for (int x = 0; x < width; ++x) {
        out[x] = sum * scale;
        sum += row[std::min(x + radius + 1, width - 1)] - row[std::max(x - radius, 0)];
      }
    }
    // Vertical: scratch -> data.
    for (int x = 0; x < width; ++x) {
      float sum = 0;
      for (int y = -radius; y <= radius; ++y) {
        sum += scratch[size_t(std::min(std::max(y, 0), height - 1)) * width + x];
      }
      for (int y = 0; y < height; ++y) {
        data[size_t(y) * width + x] = sum * scale;
        sum += scratch[size_t(std::min(y + radius + 1, height - 1)) * width + x] -
          scratch[size_t(std::max(y - radius, 0)) * width + x];
      }
    }
  }

  /// <summary>
  /// Grows the shape by radius px (a max filter, horizontal then vertical).
  /// </summary>
  void Dilate(std::vector<float> &data, int width, int height, int radius) {
    if (radius <= 0) {
      return;
    }
    std::vector<float> scratch(data.size());
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        float best = 0;
        for (int k = std::max(0, x - radius); k <= std::min(width - 1, x + radius); ++k) {
          best = std::max(best, data[size_t(y) * width + k]);
        }
        scratch[size_t(y) * width + x] = best;
      }
    }
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        float best = 0;
        for (int k = std::max(0, y - radius); k <= std::min(height - 1, y + radius); ++k) {
          best = std::max(best, scratch[size_t(k) * width + x]);
        }
        data[size_t(y) * width + x] = best;
      }
    }
  }

  /// <summary>
  /// A gaussian blur of the given sigma, approximated by three box blurs.
  /// </summary>
  void GaussianBlur(std::vector<float> &data, int width, int height, float sigma) {
    if (sigma < 0.5f) {
      return;
    }
    // Box sizes for 3 passes (Kovesi, "Fast almost-gaussian filtering").
    const int passes = 3;
    const float ideal = std::sqrt(12 * sigma * sigma / passes + 1);
    int lower = int(std::floor(ideal));
    if (lower % 2 == 0) {
      --lower;
    }
    const int upper = lower + 2;
    const int m = int(std::round((12 * sigma * sigma - passes * lower * lower - 4 * passes * lower - 3 * passes) / (-4 * lower - 4)));
    std::vector<float> scratch(data.size());
    for (int i = 0; i < passes; ++i) {
      BoxBlur(data, scratch, width, height, ((i < m ? lower : upper) - 1) / 2);
    }
  }
}


ShadowWindow::ShadowWindow()
  : mWindow(nullptr)
  , mHasRendered(false)
  , mOrigin({ 0, 0 })
  , mSize({ 0, 0 }) {
}


ShadowWindow::~ShadowWindow() {
  if (mWindow != nullptr) {
    DestroyWindow(mWindow);
  }
}


const wchar_t *ShadowWindow::ClassName() {
  return kClassName;
}


void ShadowWindow::Hide() {
  if (mWindow != nullptr && IsWindowVisible(mWindow)) {
    ShowWindow(mWindow, SW_HIDE);
  }
}


void ShadowWindow::Update(HWND target, const ShadowShape &shape, bool show) {
  bool any = false;
  for (const State::ShadowLayer &layer : shape.layers) {
    any |= layer.enabled && layer.opacity > 0;
  }
  if (!show || !any || !IsWindowVisible(target)) {
    Hide();
    return;
  }

  if (mWindow == nullptr) {
    EnsureClass();
    mWindow = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
      kClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, ThisModule(), nullptr);
    if (mWindow == nullptr) {
      return;
    }
    // VWM and the task lists leave LiteStep's own windows alone.
    SetWindowLongPtrW(mWindow, GWLP_USERDATA, magicDWord);
  }

  const float width = shape.rect.right - shape.rect.left, height = shape.rect.bottom - shape.rect.top;
  const bool sameMask = shape.mask == mRendered.mask ||
    (shape.mask && mRendered.mask && shape.mask->key == mRendered.mask->key);
  const bool sameShape = mHasRendered && sameMask && SameLayers(shape.layers, mRendered.layers) &&
    SameRadii(shape.radii, mRendered.radii) &&
    width == mRendered.rect.right - mRendered.rect.left &&
    height == mRendered.rect.bottom - mRendered.rect.top;
  if (!sameShape) {
    if (!Render(shape)) {
      Hide();
      return;
    }
    mRendered = shape;
    mHasRendered = true;
  }

  RECT targetRect;
  GetWindowRect(target, &targetRect);
  const int x = targetRect.left + int(std::floor(shape.rect.left)) + mOrigin.x;
  const int y = targetRect.top + int(std::floor(shape.rect.top)) + mOrigin.y;

  // Same band as the window (topmost or not), then right below it.
  const bool targetTopmost = (GetWindowLongPtrW(target, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
  const bool shadowTopmost = (GetWindowLongPtrW(mWindow, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
  if (targetTopmost != shadowTopmost) {
    SetWindowPos(mWindow, targetTopmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
      SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
  }
  RECT current;
  GetWindowRect(mWindow, &current);
  if (sameShape && IsWindowVisible(mWindow) && targetTopmost == shadowTopmost &&
      current.left == x && current.top == y && GetWindow(target, GW_HWNDNEXT) == mWindow) {
    return;
  }
  SetWindowPos(mWindow, target, x, y, mSize.cx, mSize.cy,
    SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
}


bool ShadowWindow::Render(const ShadowShape &shape) {
  const float shapeW = shape.rect.right - shape.rect.left;
  const float shapeH = shape.rect.bottom - shape.rect.top;
  if (shapeW < 1 || shapeH < 1) {
    return false;
  }

  // How far the layers reach outside the shape.
  float reachLeft = 0, reachTop = 0, reachRight = 0, reachBottom = 0;
  for (const State::ShadowLayer &layer : shape.layers) {
    if (!layer.enabled || layer.opacity <= 0) {
      continue;
    }
    const float grow = std::max(0.0f, layer.spread) + std::max(0.0f, layer.blurRadius) * 1.5f;
    reachLeft = std::max(reachLeft, grow - layer.offsetX);
    reachRight = std::max(reachRight, grow + layer.offsetX);
    reachTop = std::max(reachTop, grow - layer.offsetY);
    reachBottom = std::max(reachBottom, grow + layer.offsetY);
  }
  const int padLeft = int(std::ceil(reachLeft)) + 1, padTop = int(std::ceil(reachTop)) + 1;
  const int padRight = int(std::ceil(reachRight)) + 1, padBottom = int(std::ceil(reachBottom)) + 1;
  const int width = int(std::ceil(shapeW)) + padLeft + padRight;
  const int height = int(std::ceil(shapeH)) + padTop + padBottom;
  if (width > 16384 || height > 16384) {
    return false;
  }

  // Premultiplied colour, stacked over the layers ("over" operator).
  const size_t count = size_t(width) * height;
  std::vector<float> outA(count, 0), outR(count, 0), outG(count, 0), outB(count, 0);
  std::vector<float> mask(count);

  for (const State::ShadowLayer &layer : shape.layers) {
    if (!layer.enabled || layer.opacity <= 0) {
      continue;
    }
    const float spread = std::max(0.0f, layer.spread);
    const float left = padLeft - spread + layer.offsetX, top = padTop - spread + layer.offsetY;
    const float right = padLeft + shapeW + spread + layer.offsetX, bottom = padTop + shapeH + spread + layer.offsetY;
    State::CornerRadii radii = shape.radii;
    for (State::CornerRadius *corner : { &radii.topLeft, &radii.topRight, &radii.bottomRight, &radii.bottomLeft }) {
      if (corner->x > 0) {
        corner->x += spread;
      }
    }
    if (shape.mask) {
      // The picture's shape, moved by the offset and grown by the spread.
      const int dx = padLeft + int(std::round(layer.offsetX)), dy = padTop + int(std::round(layer.offsetY));
      for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
          mask[size_t(y) * width + x] = shape.mask->At(x - dx, y - dy);
        }
      }
      Dilate(mask, width, height, int(std::round(spread)));
    } else {
      for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
          mask[size_t(y) * width + x] = Coverage(float(x), float(y), left, top, right, bottom, radii);
        }
      }
    }
    GaussianBlur(mask, width, height, layer.blurRadius / 2);

    const float alpha = std::min(1.0f, std::max(0.0f, layer.opacity)) * layer.color.a;
    for (size_t i = 0; i < count; ++i) {
      const float a = mask[i] * alpha;
      const float keep = 1 - a;
      outR[i] = layer.color.r * a + outR[i] * keep;
      outG[i] = layer.color.g * a + outG[i] * keep;
      outB[i] = layer.color.b * a + outB[i] * keep;
      outA[i] = a + outA[i] * keep;
    }
  }

  // The bitmap: premultiplied BGRA, top-down, with the shape itself cut out.
  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void *bits = nullptr;
  HDC screen = GetDC(nullptr);
  HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (bitmap == nullptr) {
    ReleaseDC(nullptr, screen);
    return false;
  }
  BYTE *pixel = static_cast<BYTE*>(bits);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x, pixel += 4) {
      const size_t i = size_t(y) * width + x;
      const float cut = 1 - (shape.mask
        ? shape.mask->At(x - padLeft, y - padTop)
        : Coverage(float(x), float(y), float(padLeft), float(padTop), padLeft + shapeW, padTop + shapeH, shape.radii));
      pixel[0] = BYTE(std::min(255.0f, outB[i] * cut * 255 + 0.5f));
      pixel[1] = BYTE(std::min(255.0f, outG[i] * cut * 255 + 0.5f));
      pixel[2] = BYTE(std::min(255.0f, outR[i] * cut * 255 + 0.5f));
      pixel[3] = BYTE(std::min(255.0f, outA[i] * cut * 255 + 0.5f));
    }
  }

  HDC memory = CreateCompatibleDC(screen);
  HGDIOBJ old = SelectObject(memory, bitmap);
  POINT source = { 0, 0 };
  SIZE size = { width, height };
  BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
  const BOOL updated = UpdateLayeredWindow(mWindow, screen, nullptr, &size, memory, &source, 0, &blend, ULW_ALPHA);
  SelectObject(memory, old);
  DeleteDC(memory);
  DeleteObject(bitmap);
  ReleaseDC(nullptr, screen);

  mOrigin = { -padLeft, -padTop };
  mSize = size;
  return updated != FALSE;
}
