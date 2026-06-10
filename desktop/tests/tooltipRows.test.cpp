#include "core/tooltipRows.hpp"
#include "doctest.h"

using namespace stencil::core;

TEST_CASE("buildTooltipRows mirrors tooltip.js show() rows") {
  Point pixel{105.4, 148.6};
  Point page{10.5, 14.85};  // page (cm), post-formula
  PageSize dims{21.0, 29.7};
  TooltipRowFlags flags;  // all on

  const auto rows = buildTooltipRows(pixel, page, dims, flags);
  REQUIRE(rows.size() == 3);
  CHECK(rows[0].first == "Pixel");
  CHECK(rows[0].second == "105, 149");  // Math.round
  CHECK(rows[1].first == "Page (cm)");
  CHECK(rows[1].second == "10.50, 14.85");  // toFixed(2)
  CHECK(rows[2].first == "To edge (cm)");
  CHECK(rows[2].second == "10.50, 14.85");  // 21-10.5, 29.7-14.85
}

TEST_CASE("buildTooltipRows converts page/to-edge into the chosen unit") {
  Point pixel{105.4, 148.6};
  Point page{10.5, 14.85};
  PageSize dims{21.0, 29.7};
  TooltipRowFlags flags;
  UnitFormat inches{1.0 / 2.54, "in"};

  const auto rows = buildTooltipRows(pixel, page, dims, flags, inches);
  REQUIRE(rows.size() == 3);
  CHECK(rows[0].second == "105, 149");      // pixels never convert
  CHECK(rows[1].first == "Page (in)");
  CHECK(rows[1].second == "4.13, 5.85");    // 10.5/2.54, 14.85/2.54
  CHECK(rows[2].first == "To edge (in)");
  CHECK(rows[2].second == "4.13, 5.85");
}

TEST_CASE("buildTooltipRows respects the row toggles") {
  Point pixel{0, 0};
  Point page{0, 0};
  PageSize dims{21.0, 29.7};
  TooltipRowFlags flags;
  flags.showScreen = false;
  flags.showCoords = false;
  const auto rows = buildTooltipRows(pixel, page, dims, flags);
  REQUIRE(rows.size() == 1);
  CHECK(rows[0].first == "Page (cm)");
}
