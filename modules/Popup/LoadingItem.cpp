/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *  LoadingItem.cpp
 *  The nModules Project
 *
 *  The row a content popup shows while its content is being read.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#include "../ModuleKit/LiteStep.h"
#include "LoadingItem.hpp"
#include "../ModuleKit/Color.h"
#include "../ModuleKit/FrameClock.hpp"
#include "../ModuleKit/IColorVal.hpp"
#include "../ModuleKit/Window.hpp"

#include <cmath>
#include <memory>

namespace {
  // Content that arrives sooner than this shows no text and no spinner.
  const float kDelaySeconds = 0.15f;
  // One full turn of the spinner.
  const float kTurnSeconds = 0.8f;
  const int kSpokes = 12;
  // Spinner geometry, in pixels.
  const float kOuterRadius = 5.0f;
  const float kInnerRadius = 2.5f;
  const float kStroke = 1.5f;
  // Gap between the spinner and where the row's text area starts.
  const float kMarginLeft = 6.0f;
}


LoadingItem::LoadingItem(Drawable *parent, LPCTSTR text)
    : InfoItem(parent, L"")
    , mText(text)
{
    // The spinner takes the colour of the row's text, and sits where the text area starts.
    std::unique_ptr<IColorVal> white = Color::Create(0xFFFFFFFF);
    std::unique_ptr<IColorVal> color(mSettings->GetColor(L"FontColor", white.get()));
    mSpinner.color = color->Evaluate();
    mSpinner.offsetLeft = float(mSettings->GetInt(L"TextOffsetLeft", 0)) + kMarginLeft;

    mWindow->AddPostPainter(&mSpinner);
    mClock.Clock();
    mWindow->RequestFrames(this);
}


LoadingItem::~LoadingItem()
{
    mWindow->StopFrames(this);
    mSpinner.DiscardDeviceResources();
}


LRESULT LoadingItem::HandleMessage(HWND window, UINT msg, WPARAM wParam, LPARAM lParam, LPVOID data)
{
    if (msg == FrameClock::Message())
    {
        OnFrame();
        return 0;
    }
    return InfoItem::HandleMessage(window, msg, wParam, lParam, data);
}


void LoadingItem::OnFrame()
{
    float elapsed = mClock.GetTime();
    if (elapsed < kDelaySeconds)
    {
        return;
    }
    if (!mShown)
    {
        mShown = true;
        mSpinner.visible = true;
        mWindow->SetText(mText.c_str());
    }

    // Repaint only when the bright spoke moves, not on every frame.
    int head = int(std::fmod(elapsed, kTurnSeconds) / kTurnSeconds * kSpokes) % kSpokes;
    if (head != mSpinner.head || elapsed - kDelaySeconds < 0.02f)
    {
        mSpinner.head = head;
        mWindow->Repaint();
    }
}


void LoadingItem::Spinner::Paint(ID2D1RenderTarget *renderTarget)
{
    if (!this->visible)
    {
        return;
    }
    if (mBrush == nullptr && FAILED(ReCreateDeviceResources(renderTarget)))
    {
        return;
    }

    const float pi = 3.14159265f;
    float cx = mArea.left + this->offsetLeft + kOuterRadius;
    float cy = std::floor((mArea.top + mArea.bottom) / 2.0f) + 0.5f;
    float baseAlpha = float((this->color >> 24) & 0xFF) / 255.0f;

    for (int i = 0; i < kSpokes; ++i)
    {
        // The head is fully opaque; the spokes behind it fade out.
        int behind = (this->head - i + kSpokes) % kSpokes;
        float alpha = baseAlpha * (1.0f - float(behind) / float(kSpokes) * 0.85f);
        float angle = 2.0f * pi * float(i) / float(kSpokes) - pi / 2.0f;
        float c = std::cos(angle), s = std::sin(angle);

        mBrush->SetOpacity(alpha);
        renderTarget->DrawLine(
            D2D1::Point2F(cx + c * kInnerRadius, cy + s * kInnerRadius),
            D2D1::Point2F(cx + c * kOuterRadius, cy + s * kOuterRadius),
            mBrush, kStroke);
    }
}


void LoadingItem::Spinner::DiscardDeviceResources()
{
    if (mBrush != nullptr)
    {
        mBrush->Release();
        mBrush = nullptr;
    }
}


HRESULT LoadingItem::Spinner::ReCreateDeviceResources(ID2D1RenderTarget *renderTarget)
{
    DiscardDeviceResources();
    D2D1_COLOR_F color = D2D1::ColorF(
        float((this->color >> 16) & 0xFF) / 255.0f,
        float((this->color >> 8) & 0xFF) / 255.0f,
        float(this->color & 0xFF) / 255.0f,
        1.0f);
    return renderTarget->CreateSolidColorBrush(color, &mBrush);
}


void LoadingItem::Spinner::UpdatePosition(D2D1_RECT_F parentPosition)
{
    mArea = parentPosition;
}


bool LoadingItem::Spinner::UpdateDWMColor(ARGB, ID2D1RenderTarget*)
{
    return false;
}
