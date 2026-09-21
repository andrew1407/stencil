#pragma once
// Shared ground for the theme-wipe headless TUs: the area-coverage probes both halves judge the
// curve on, and the section the dust TU contributes.
#include "ThemeSwapOverlay.hpp"

#include <QApplication>
#include <cmath>
#include <cstdio>
#include <string>

using stencil::gui::ThemeSwapOverlay;

#include "../../support/check.hpp"

// Fraction of a w×h window covered by a circle of radius `r` about (cx, cy), sampled.
inline double covered(double r, double cx, double cy, double w, double h) {
  const int n = 90;
  int inside = 0;
  for (int i = 0; i < n; i++)
    for (int j = 0; j < n; j++) {
      const double x = (i + 0.5) * w / n, y = (j + 0.5) * h / n;
      if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) inside++;
    }
  return double(inside) / (n * n);
}

// What `start()` does: the radius reaches the furthest corner from the origin.
inline double fullRadius(double cx, double cy, double w, double h) {
  return std::hypot(std::max(cx, w - cx), std::max(cy, h - cy));
}

void dustGrainCurve(double w, double h);
