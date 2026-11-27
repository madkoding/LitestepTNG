//-------------------------------------------------------------------------------------------------
// /ModuleKit/DropShadow.cpp
// The nModules Project
//
// See DropShadow.hpp.
//-------------------------------------------------------------------------------------------------
#include "DropShadow.hpp"

#include <d2d1_1.h>
#include <d2d1effects.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "dxguid.lib")

namespace {
  template <class T> void Release(T *&p) {
    if (p != nullptr) {
      p->Release();
      p = nullptr;
    }
  }
}


void DropShadow::PaintBitmapShadow(ID2D1RenderTarget *target, ID2D1Bitmap *bitmap, const D2D1_RECT_F &dest,
    const Params &params) {
  if (!params.Enabled() || target == nullptr || bitmap == nullptr) {
    return;
  }
  const D2D1_SIZE_F size = bitmap->GetSize();
  const float width = dest.right - dest.left, height = dest.bottom - dest.top;
  if (size.width <= 0 || size.height <= 0 || width <= 0 || height <= 0) {
    return;
  }

  ID2D1DeviceContext *context = nullptr;
  if (FAILED(target->QueryInterface(&context))) {
    return;
  }
  ID2D1Effect *shadow = nullptr;
  if (SUCCEEDED(context->CreateEffect(CLSID_D2D1Shadow, &shadow))) {
    shadow->SetInput(0, bitmap);
    // The effect works in the bitmap's own pixels; the blur is given in screen px.
    const float scale = (std::max)(width / size.width, height / size.height);
    shadow->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, (std::max)(0.0f, params.blur / 2 / scale));
    shadow->SetValue(D2D1_SHADOW_PROP_COLOR,
      D2D1::Vector4F(params.color.r, params.color.g, params.color.b, params.color.a));

    D2D1_MATRIX_3X2_F old;
    context->GetTransform(&old);
    context->SetTransform(
      D2D1::Matrix3x2F::Scale(width / size.width, height / size.height) *
      D2D1::Matrix3x2F::Translation(dest.left + params.offsetX, dest.top + params.offsetY) * old);
    context->DrawImage(shadow);
    context->SetTransform(old);
    Release(shadow);
  }
  Release(context);
}


void DropShadow::PaintTextShadow(ID2D1RenderTarget *target, IDWriteTextLayout *layout, D2D1_POINT_2F origin,
    const Params &params) {
  if (!params.Enabled() || target == nullptr || layout == nullptr) {
    return;
  }

  if (params.blur < 0.5f) {
    // A crisp shadow: the text again, behind, in the shadow colour.
    ID2D1SolidColorBrush *brush = nullptr;
    if (SUCCEEDED(target->CreateSolidColorBrush(params.color, &brush))) {
      target->DrawTextLayout(D2D1::Point2F(origin.x + params.offsetX, origin.y + params.offsetY), layout, brush,
        D2D1_DRAW_TEXT_OPTIONS_CLIP);
      Release(brush);
    }
    return;
  }

  // A soft shadow: the text drawn alone into a bitmap the size of the layout box (plus
  // room for the blur), then that bitmap through the shadow effect.
  const float margin = std::ceil(params.blur * 1.5f);
  const float boxW = layout->GetMaxWidth(), boxH = layout->GetMaxHeight();
  if (boxW <= 0 || boxH <= 0 || boxW > 8192 || boxH > 8192) {
    return;
  }
  const D2D1_SIZE_F size = D2D1::SizeF(std::ceil(boxW + margin * 2), std::ceil(boxH + margin * 2));
  ID2D1BitmapRenderTarget *offscreen = nullptr;
  if (FAILED(target->CreateCompatibleRenderTarget(size, &offscreen))) {
    return;
  }
  ID2D1SolidColorBrush *brush = nullptr;
  offscreen->BeginDraw();
  offscreen->Clear(D2D1::ColorF(0, 0, 0, 0));
  offscreen->SetTextAntialiasMode(target->GetTextAntialiasMode());
  if (SUCCEEDED(offscreen->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &brush))) {
    offscreen->DrawTextLayout(D2D1::Point2F(margin, margin), layout, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    Release(brush);
  }
  ID2D1Bitmap *bitmap = nullptr;
  if (SUCCEEDED(offscreen->EndDraw()) && SUCCEEDED(offscreen->GetBitmap(&bitmap))) {
    PaintBitmapShadow(target, bitmap,
      D2D1::RectF(origin.x - margin, origin.y - margin, origin.x - margin + size.width, origin.y - margin + size.height),
      params);
    Release(bitmap);
  }
  Release(offscreen);
}
