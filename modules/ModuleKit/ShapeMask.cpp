//-------------------------------------------------------------------------------------------------
// /ModuleKit/ShapeMask.cpp
// The nModules Project
//
// See ShapeMask.hpp. The layouts copy Brush::ScaleImage and Brush::ComputeEdgeData, including
// their quirks (the middle of an Edges image is scaled by the whole width, as Brush does), so
// that the mask lies exactly under what is painted.
//-------------------------------------------------------------------------------------------------
#include "LiteStep.h"
#include "Factories.h"
#include "ShapeMask.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <strsafe.h>
#include <wincodec.h>

namespace {
  struct Picture {
    UINT width = 0;
    UINT height = 0;
    std::vector<BYTE> alpha;
  };

  /// <summary>
  /// The alpha channel of a picture, read the way Brush::LoadImageFile reads it. Kept per file.
  /// </summary>
  const Picture *LoadPicture(LPCWSTR path) {
    static std::map<std::wstring, std::unique_ptr<Picture>> cache;
    auto found = cache.find(path);
    if (found != cache.end()) {
      return found->second.get();
    }

    std::unique_ptr<Picture> picture;
    IWICImagingFactory *factory = nullptr;
    HBITMAP hBitmap = LiteStep::LoadLSImage(path, nullptr);
    if (hBitmap != nullptr && SUCCEEDED(Factories::GetWICFactory(reinterpret_cast<LPVOID*>(&factory)))) {
      IWICBitmap *wicBitmap = nullptr;
      IWICFormatConverter *converter = nullptr;
      if (SUCCEEDED(factory->CreateBitmapFromHBITMAP(hBitmap, nullptr, WICBitmapUseAlpha, &wicBitmap)) &&
          SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
          SUCCEEDED(converter->Initialize(wicBitmap, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
            nullptr, 0.f, WICBitmapPaletteTypeMedianCut))) {
        UINT width = 0, height = 0;
        converter->GetSize(&width, &height);
        std::vector<BYTE> pixels(size_t(width) * height * 4);
        if (width > 0 && height > 0 &&
            SUCCEEDED(converter->CopyPixels(nullptr, width * 4, UINT(pixels.size()), pixels.data()))) {
          picture.reset(new Picture());
          picture->width = width;
          picture->height = height;
          picture->alpha.resize(size_t(width) * height);
          for (size_t i = 0; i < picture->alpha.size(); ++i) {
            picture->alpha[i] = pixels[i * 4 + 3];
          }
        }
      }
      if (converter != nullptr) {
        converter->Release();
      }
      if (wicBitmap != nullptr) {
        wicBitmap->Release();
      }
    }
    if (hBitmap != nullptr) {
      DeleteObject(hBitmap);
    }

    const Picture *result = picture.get();
    cache[path] = std::move(picture);
    return result;
  }

  /// <summary>
  /// The picture's coordinate shown at window coordinate x for one axis of an Edges brush
  /// (Brush::ComputeEdgeData): low and high are the trimmed edges.
  /// </summary>
  float EdgesSource(float x, float size, float picture, float low, float high) {
    if (x < low) {
      return x;
    }
    if (x >= size - high) {
      return x - (size - picture);
    }
    return low + (x - low) * (picture - low - high) / size;
  }
}


std::shared_ptr<const ShapeMask> ShapeMasks::FromBrush(const BrushSettings &brush, int width, int height) {
  if (_wcsicmp(brush.brushType, L"Image") != 0 || brush.image == nullptr || brush.image[0] == L'\0' ||
      width <= 0 || height <= 0 || width > 16384 || height > 16384) {
    return nullptr;
  }
  const Picture *picture = LoadPicture(brush.image);
  if (picture == nullptr) {
    return nullptr;
  }

  const float pw = float(picture->width), ph = float(picture->height);
  const float w = float(width), h = float(height);
  const float opacity = std::max(0.0f, std::min(1.0f, brush.imageOpacity));
  LPCWSTR mode = brush.imageScalingMode;

  std::shared_ptr<ShapeMask> mask(new ShapeMask());
  mask->width = width;
  mask->height = height;
  mask->alpha.resize(size_t(width) * height);
  WCHAR key[MAX_PATH + 160];
  StringCchPrintfW(key, _countof(key), L"%s|%s|%.1f,%.1f,%.1f,%.1f|%.3f|%dx%d", brush.image, mode,
    brush.imageEdges.left, brush.imageEdges.top, brush.imageEdges.right, brush.imageEdges.bottom,
    opacity, width, height);
  mask->key = key;

  // Edges: trimmed like Brush::ComputeEdgeData when the window is smaller than the edges.
  D2D1_RECT_F edges = brush.imageEdges;
  if (edges.left + edges.right > w) {
    const float scale = w / (edges.left + edges.right);
    edges.left *= scale;
    edges.right *= scale;
  }
  if (edges.top + edges.bottom > h) {
    const float scale = h / (edges.top + edges.bottom);
    edges.top *= scale;
    edges.bottom *= scale;
  }
  const float fit = std::min(w / pw, h / ph), fill = std::max(w / pw, h / ph);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const float cx = x + 0.5f, cy = y + 0.5f;
      float u, v;
      if (_wcsicmp(mode, L"Edges") == 0) {
        u = EdgesSource(cx, w, pw, edges.left, edges.right);
        v = EdgesSource(cy, h, ph, edges.top, edges.bottom);
      } else if (_wcsicmp(mode, L"Stretch") == 0) {
        u = cx * pw / w;
        v = cy * ph / h;
      } else if (_wcsicmp(mode, L"Tile") == 0) {
        u = std::fmod(cx, pw);
        v = std::fmod(cy, ph);
      } else if (_wcsicmp(mode, L"Fit") == 0 || _wcsicmp(mode, L"Fill") == 0) {
        const float scale = _wcsicmp(mode, L"Fit") == 0 ? fit : fill;
        u = (cx - w / 2) / scale + pw / 2;
        v = (cy - h / 2) / scale + ph / 2;
      } else {  // Center
        u = cx - (w - pw) / 2;
        v = cy - (h - ph) / 2;
      }
      const int iu = int(std::floor(u)), iv = int(std::floor(v));
      float a = 0;
      if (iu >= 0 && iv >= 0 && iu < int(picture->width) && iv < int(picture->height)) {
        a = picture->alpha[size_t(iv) * picture->width + iu] / 255.0f;
      }
      mask->alpha[size_t(y) * width + x] = a * opacity;
    }
  }
  return mask;
}
