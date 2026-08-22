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
    double pointSize = 4.0;
    std::string style = "solid";        // solid | dashed | dotted
    bool locked = false;                // closed polygon (area) when true
    std::string fillColor = "transparent";
    // Point colour, set independently of the stroke. EMPTY MEANS INHERIT `color`
    // — that is the back-compatible default, so every layout/project written before this
    // field existed keeps drawing points in the line colour exactly as it used to.
    // Read it through pointColorOr() rather than directly.
    std::string pointColor = "";
  };

  // The colour a line's points actually draw in: its own pointColor when set,
  // otherwise the stroke colour. One helper so no call site re-implements the fallback.
  inline const std::string& pointColorOr(const Line& line) {
    return line.pointColor.empty() ? line.color : line.pointColor;
  }

  using Lines = std::vector<Line>;

}
