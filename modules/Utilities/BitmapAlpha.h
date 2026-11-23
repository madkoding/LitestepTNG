//-------------------------------------------------------------------------------------------------
// /Utilities/BitmapAlpha.h
// The nModules Project
//
// Alpha channel fix for bitmaps handed out by the shell.
//-------------------------------------------------------------------------------------------------
#pragma once

#include <Windows.h>

namespace BitmapAlpha {
  /// <summary>
  /// The shell returns thumbnails as 32-bit bitmaps whose alpha bytes are sometimes all
  /// zero (a picture has no transparency, and nobody filled them in). Drawn with alpha
  /// blending such a bitmap is invisible. If no pixel has any alpha, the whole bitmap is
  /// made opaque; a bitmap with real transparency (an icon) is left alone.
  /// </summary>
  inline void MakeOpaqueIfMissing(HBITMAP bitmap) {
    DIBSECTION section;
    if (GetObjectW(bitmap, sizeof(section), &section) != sizeof(section)
        || section.dsBm.bmBitsPixel != 32 || section.dsBm.bmBits == nullptr) {
      return;
    }
    DWORD *pixels = static_cast<DWORD*>(section.dsBm.bmBits);
    const size_t count = size_t(section.dsBm.bmWidth)
      * size_t(section.dsBm.bmHeight < 0 ? -section.dsBm.bmHeight : section.dsBm.bmHeight);
    for (size_t i = 0; i < count; ++i) {
      if ((pixels[i] & 0xFF000000) != 0) {
        return;
      }
    }
    for (size_t i = 0; i < count; ++i) {
      pixels[i] |= 0xFF000000;
    }
  }
}
