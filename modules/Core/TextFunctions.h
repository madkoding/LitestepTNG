/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *  TextFormatting.cpp
 *  The nModules Project
 *
 *  Built-in text functions.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma once

#include "IParsedText.hpp"

namespace TextFunctions {
  void _Register();
  void _Unregister();

  // Takes a new CPU/memory sample. Called once per second by the core timer.
  void _Sample();

  // Media players: deferred start-up work, and reloading the list on !Refresh.
  void _PlayerTick();
  void _Refresh();
}
