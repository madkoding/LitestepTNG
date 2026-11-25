/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *  Taskbar.hpp
 *  The nModules Project
 *
 *  Represents a taskbar. Contains TaskButtons.
 *  
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma once

#include <map>
#include "ButtonSettings.hpp"
#include "TaskButton.hpp"
#include "../ModuleKit/Window.hpp"
#include "../ModuleKit/MessageHandler.hpp"
#include "../ModuleKit/LayoutSettings.hpp"
#include "../ModuleKit/Drawable.hpp"
#include "../ModuleKit/WindowThumbnail.hpp"

class Taskbar: public Drawable
{
    // Public Typedefs
public:
    // Ways to sort the tasks.
    enum class SortingType
    {
        Application,
        Title,
        TimeAdded,
        Position
    };

    // Private Typedefs
private:
    typedef std::list<TaskButton> ButtonList;
    typedef std::map<HWND, ButtonList::iterator> ButtonMap;

    enum class States
    {
        Base = 0,
        Count
    };

    // Constructors and destructors
public:
    explicit Taskbar(LPCTSTR);
    ~Taskbar();

    // MessageHandler
public:
    LRESULT WINAPI HandleMessage(HWND window, UINT msg, WPARAM wParam, LPARAM lParam, LPVOID Window) override;

    // Public methods
public:
    void ShowThumbnail(HWND hwnd, LPRECT position);
    void HideThumbnail();

    void LoadSettings(bool = false);
    TaskButton *AddTask(HWND, UINT, bool);
    bool MonitorChanged(HWND hWnd, UINT monitor, TaskButton **out);
    void RemoveTask(HWND);
    void Relayout();
    void Repaint();

    // Private methods
private:
    // Removes a task from this taskbar
    void RemoveTask(ButtonMap::iterator iter);

    // 
private:
    // Settings which define how to organize the buttons
    LayoutSettings mLayoutSettings;

    // The taskbar buttons
    ButtonSettings mButtonSettings;
    ButtonMap mButtonMap;
    ButtonList mButtonList;

    // The maximum width of a taskbar button
    Distance mButtonMaxWidth;
    Distance mButtonMaxHeight;
    Distance mButtonWidth;
    Distance mButtonHeight;

    //
    StateRender<States> mStateRender;

    // The monitor to display tasks for
    UINT mMonitor;

    //
    SortingType mSortingType;

    //
    WindowThumbnail* mThumbnail;

    //
    bool mNoThumbnails;

    // Layout animation ("AnimationDuration", ms; 0 = buttons jump to their place).
    // When tasks are added or removed the buttons slide to their new positions.
    int mAnimationDuration = 0;
    // True while the buttons are sliding (frames requested from the window).
    bool mAnimationRunning = false;
    // High-resolution: GetTickCount64 moves in ~15.6 ms steps, coarser than a frame.
    StopWatch mAnimationClock;
};

