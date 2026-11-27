/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *  WindowSettings.hpp
 *  The nModules Project
 *
 *  Settings used by Window.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma once

#include "Settings.hpp"

#include "../Utilities/CommonD2D.h"

#include <dwrite.h>

class WindowSettings {
public:
    WindowSettings();
    explicit WindowSettings(void init(WindowSettings &));

public:
    // Loads the actual settings.
    void Load(const Settings *settings, const WindowSettings *defaults = nullptr);

    D2D1_TEXT_ANTIALIAS_MODE ParseAntiAliasMode(LPCTSTR mode);

    // True if the window should be topmost. Default: false
    bool alwaysOnTop;

    // True if the window background should be blured. Default: false
    bool blurBehind;

    // True in order to let the
    bool clickThrough;

    // True to evaluate text strings. Not read from RC files. Default: false
    bool evaluateText;

    // The height of the window. Default: 100dip
    Distance height;

    // True if the window should start hidden. Default: false
    bool hidden;

    // True to keep the window on the desktop: under every other program's window,
    // just above the shell's desktop. Implies not topmost. Default: false
    bool onDesktop;

    // True to register this drawable with the core. Not read from RC files. Default: false
    bool registerWithCore;

    // Entry animation for top-level windows: fade-in duration in ms. Default: 0 (disabled)
    int showAnimationDuration;

    // Pixels the window slides vertically while fading in. Positive = rises from below,
    // negative = drops from above. Default: 0
    int showAnimationOffset;

    // Horizontal slide of the entry animation, px (ShowAnimationOffsetX). Default: 0
    int showAnimationOffsetX;

    // Exit animation of HideAnimated(): duration in ms (0 = none) and where the window
    // slides to while it fades (HideAnimationDuration, HideAnimationOffset[X]). Default: 0
    int hideAnimationDuration;
    int hideAnimationOffset;
    int hideAnimationOffsetX;

    // The text to display. Default: "" (blank)
    TCHAR text[MAX_LINE_LENGTH];

    // How to anti-alias the text.
    D2D1_TEXT_ANTIALIAS_MODE textAntiAliasMode;

    // True to render all text in upper case. Read from "TextUppercase", with a
    // global "TextUppercase" fallback so one line applies to every drawable.
    // Default: false
    bool textUppercase;

    // The width of the window. Default: 100dip
    Distance width;

    // The x position of the window, relative to the parent. Default: 0
    Distance x;

    // The y position of the window, relative to the parent. Default: 0
    Distance y;
};
