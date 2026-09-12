#pragma once
#include <string>
#include <vector>

// Shared data models. Mirrors the plain line objects of browser/js/core/drawingApp.js.
namespace stencil::core {

  // Image-pixel space.
  struct Point {
    double x = 0.0;
    double y = 0.0;
  };

  struct Line {
    std::vector<Point> points;
    std::string color = "#FFFF00";
    double thickness = 2.0;
    double pointSize = 4.0;
    std::string style = "solid";        // solid | dashed | dotted
    bool locked = false;                // closed polygon (area) when true
    std::string fillColor = "transparent";
    // EMPTY MEANS INHERIT `color` (layouts written before the field existed carry
    // none). Read it through pointColorOr(), never directly.
    std::string pointColor = "";
  };

  inline const std::string& pointColorOr(const Line& line) {
    return line.pointColor.empty() ? line.color : line.pointColor;
  }

  typedef std::vector<Line> Lines;

}
