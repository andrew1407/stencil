#pragma once
#include "colorNames.hpp"  // Rgba
#include "models.hpp"
#include "rasterize.hpp"  // DashPattern

#include <cstddef>
#include <cstdint>
#include <vector>

// A line's stroke as one coverage pass, row by row: a pixel takes the max coverage over the
// path's segments and blends once, as the canvas stroke of browser/js/core/line/render.js
// does, so translucent ink never piles up where the path meets itself. Only rasterize.cpp
// draws with it.
namespace stencil::core::coverage {

  // `thickness` clamps to MAX_STROKE_THICKNESS; `dash` nullptr strokes solid.
  void strokePolyline(std::uint8_t* buf, int w, int h, const std::vector<Point>& pts,
                      bool closed, double thickness, const DashPattern* dash, const Rgba& c);

  // A stroke's cost in pixels: `visits` examined (each coverage evaluation, each pixel of the
  // blend pass), `blends` written.
  struct StrokeWork {
    std::size_t visits = 0;
    std::size_t blends = 0;
  };

  // The same pass counted instead of drawn: the tests' deterministic measure of its work.
  StrokeWork strokeWork(int w, int h, const std::vector<Point>& pts, bool closed,
                        double thickness, const DashPattern* dash);

}  // namespace stencil::core::coverage
