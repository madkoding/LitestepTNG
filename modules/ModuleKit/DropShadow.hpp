//-------------------------------------------------------------------------------------------------
// /ModuleKit/DropShadow.hpp
// The nModules Project
//
// Drop shadows that follow what is drawn (text, a picture with transparency), painted in the
// same render target, under the thing itself. Shadows of whole top-level windows are
// ShadowWindow's job; these are for things inside a window, like a desktop icon's picture
// and caption.
//-------------------------------------------------------------------------------------------------
#pragma once

#include <d2d1.h>
#include <dwrite.h>

namespace DropShadow {
  struct Params {
    D2D1_COLOR_F color = { 0, 0, 0, 0 };  // transparent = no shadow
    float offsetX = 1;
    float offsetY = 1;
    float blur = 2;                        // px; the gaussian's sigma is blur / 2

    bool Enabled() const { return color.a > 0; }
  };

  /// <summary>
  /// Paints the shadow of a bitmap that is drawn into dest (scaled to it).
  /// Needs Direct2D 1.1 (Windows 8 and later); does nothing without it.
  /// </summary>
  void PaintBitmapShadow(ID2D1RenderTarget *target, ID2D1Bitmap *bitmap, const D2D1_RECT_F &dest, const Params &params);

  /// <summary>
  /// Paints the shadow of a text layout drawn at origin. Without blur it is the text again,
  /// in the shadow colour, offset; with blur it goes through the Direct2D shadow effect.
  /// </summary>
  void PaintTextShadow(ID2D1RenderTarget *target, IDWriteTextLayout *layout, D2D1_POINT_2F origin, const Params &params);
}
