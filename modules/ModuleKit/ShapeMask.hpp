//-------------------------------------------------------------------------------------------------
// /ModuleKit/ShapeMask.hpp
// The nModules Project
//
// The shape of a window whose background is a picture: the picture's alpha, laid out at the
// window's size the way Brush draws it (Stretch, Tile, Center, Fit, Fill, Edges). A drop
// shadow cast by this mask follows cut corners and diagonal edges instead of the rectangle.
//-------------------------------------------------------------------------------------------------
#pragma once

#include "BrushSettings.hpp"

#include <memory>
#include <string>
#include <vector>

struct ShapeMask {
  int width = 0;
  int height = 0;
  // Row by row, 0..1.
  std::vector<float> alpha;
  // What it was built from (image, layout, size): equal keys, equal masks.
  std::wstring key;

  float At(int x, int y) const {
    return x < 0 || y < 0 || x >= width || y >= height ? 0.0f : alpha[size_t(y) * width + x];
  }
};

namespace ShapeMasks {
  /// <summary>
  /// The mask of a background brush of type Image at the given size, or nullptr for any other
  /// brush (or an image that cannot be read).
  /// </summary>
  std::shared_ptr<const ShapeMask> FromBrush(const BrushSettings &brush, int width, int height);
}
