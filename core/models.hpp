#pragma once
#include <string>
#include <vector>

// Shared data models for the Stencil core. Mirrors the plain objects the browser
// app stores in `lines` (see browser/js/core/drawingApp.js). GUI-free: no Qt here.
namespace stencil::core {

  // A single annotated point in image-pixel space.
  struct Point {
    double x = 0.0;
    double y = 0.0;
  };

  // A polyline / rectangle / locked area, matching the browser line object.
  struct Line {
    std::vector<Point> points;
    std::string color = "#FFFF00";
    double thickness = 2.0;
    double markerSize = 4.0;
    std::string style = "solid";        // solid | dashed | dotted
    bool locked = false;                // closed polygon (area) when true
    std::string fillColor = "transparent";
  };

  using Lines = std::vector<Line>;

}
