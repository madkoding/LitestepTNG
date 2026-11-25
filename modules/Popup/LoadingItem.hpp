/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *  LoadingItem.hpp
 *  The nModules Project
 *
 *  The row a content popup shows while a worker thread reads its content: the
 *  info item's skin, a text ("<Popup>LoadingText") and a small spinner. Both
 *  stay hidden for the first moments, so content that arrives quickly never
 *  flashes a spinner.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma once

#include "InfoItem.hpp"
#include "../ModuleKit/IPainter.hpp"
#include "../Utilities/StopWatch.hpp"

class LoadingItem : public InfoItem
{
public:
    explicit LoadingItem(Drawable *parent, LPCTSTR text);
    virtual ~LoadingItem();

    LRESULT WINAPI HandleMessage(HWND, UINT, WPARAM, LPARAM, LPVOID) override;

private:
    // Twelve short spokes around a circle, the brightest one turning; drawn after the
    // item's own skin and text.
    class Spinner : public IPainter
    {
    public:
        void Paint(ID2D1RenderTarget *renderTarget) override;
        void DiscardDeviceResources() override;
        HRESULT ReCreateDeviceResources(ID2D1RenderTarget *renderTarget) override;
        void UpdatePosition(D2D1_RECT_F parentPosition) override;
        bool UpdateDWMColor(ARGB newColor, ID2D1RenderTarget *renderTarget) override;

        ARGB color = 0xFF000000;
        int head = 0;
        bool visible = false;
        float offsetLeft = 0.0f;

    private:
        D2D1_RECT_F mArea = {};
        ID2D1SolidColorBrush *mBrush = nullptr;
    };

    void OnFrame();

    Spinner mSpinner;
    StopWatch mClock;
    std::wstring mText;
    bool mShown = false;
};
