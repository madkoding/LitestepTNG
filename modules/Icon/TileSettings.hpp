//-------------------------------------------------------------------------------------------------
// /Icon/TileSettings.hpp
// The nModules Project
//
// Contains all the settings used by an icon tile.
//-------------------------------------------------------------------------------------------------
#pragma once

#include "Tile.hpp"

#include "../ModuleKit/Settings.hpp"
#include "../ModuleKit/StateRender.hpp"
#include "../ModuleKit/WindowSettings.hpp"

class TileSettings {
public:
  WindowSettings mTileWindowSettings;
  StateRender<Tile::State> mTileStateRender;
  int mIconSize;
  // Space between the top of the tile and the icon, so a selected or hovered tile
  // shows its background around the icon and not only below it. Default: 0
  int mIconOffsetTop;
  float mGhostOpacity;

public:
  void Load(Settings *settings);
};

