#pragma once
#include "models.hpp"
#include <cstddef>
#include <vector>

// Marshalling shared by the two extern "C" ABIs (wasmApi.cpp, cliApi.cpp).
// Both receive a point list as one flat, caller-owned [x0,y0,x1,y1,…] array of
// `count` pairs — 2 * count doubles. Nothing here owns or frees caller memory.
namespace stencil::core::abi {

  inline std::vector<Point> toPoints(const double* pts, int count) {
    std::vector<Point> v;
    if (pts == nullptr || count <= 0) return v;
    v.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) v.push_back(Point{pts[2 * i], pts[2 * i + 1]});
    return v;
  }

}
