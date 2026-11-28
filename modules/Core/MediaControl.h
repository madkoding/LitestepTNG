//-------------------------------------------------------------------------------------------------
// /Core/MediaControl.h
// The nModules Project
//
// Media playback: the session being controlled, the transport bangs, the master volume
// and the list of players a theme can rotate between.
//
// Text functions: [NowPlaying], [MediaPlayer]
// Bangs:          !MediaPlayPause !MediaPlay !MediaPause !MediaStop !MediaNext !MediaPrev
//                 !MediaSeek <0-100>   !MediaVolume <0-100 | +n | -n>
//                 !MediaPlayerNext   !MediaPlayer <name>   !MediaPlayerLaunch
// Exports:        MediaGetPermille(what) / MediaSetPermille(what, permille)
//                 (0 = master volume, 1 = track position), used by the nLabel sliders
//-------------------------------------------------------------------------------------------------
#pragma once

namespace MediaControl {
  void Register();
  void Unregister();

  // Once per second, from the core timer.
  void Tick();

  // The .rc files were read again.
  void Refresh();

  // A keyboard's volume and media keys (LM_APPCOMMAND, from the shell hook). Handled only
  // when Explorer is not the shell, since Explorer acts on them itself. True = handled.
  bool HandleAppCommand(LPARAM lParam);
}