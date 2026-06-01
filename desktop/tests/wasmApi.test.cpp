// Native coverage for the WebAssembly ABI surface (desktop/core/wasmApi.cpp).
// These extern "C" wrappers are what the browser app calls through ccall/cwrap;
// emcc is not installed in this environment, so we compile the same translation
// unit natively (see CMakeLists) and exercise every export here. The wrappers
// are thin, so this guards the marshalling (flat arrays, output pointers, enum
// codes, char-code var names) rather than the already-tested core math.
#include "doctest.h"
#include <cstdint>

// Prototypes of the exported C surface (wasmApi.cpp has no header by design —
// it is an ABI boundary, not a C++ API).
extern "C" {
  double stencil_distToSegment(double, double, double, double, double, double);
  int stencil_shouldCloseShape(const double*, int, double, double, double);
  void stencil_pageDimensions(const char*, int, int, double, double, double*,
                              double*);
  void stencil_pixelToPageRaw(double, double, double, double, int, int, double*,
                              double*);
  int stencil_formulaValidate(const char*, int);
  double stencil_formulaApply(const char*, int, double, int);
  int stencil_formulaEvaluate(const char*, int, double, double*);
  void stencil_applyFilterRGBA(int, std::uint8_t*, int, int, int, int);
  void stencil_rotatePoints(double*, int, double, double, double);
  void stencil_boundingBoxCenter(const double*, int, double*);
  double stencil_clampScale(double);
  void stencil_anchoredZoom(double, double, double, double, double, double,
                            double*);
  void stencil_rectZoom(double, double, double, double, double, double, double*);
}

TEST_SUITE("wasmApi") {
  TEST_CASE("stencil_distToSegment forwards to the core") {
    CHECK(stencil_distToSegment(5, 3, 0, 0, 10, 0) == doctest::Approx(3.0));
  }

  TEST_CASE("stencil_shouldCloseShape unpacks the flat point array") {
    const double pts[] = {0, 0, 10, 0, 10, 10};  // 3 points
    CHECK(stencil_shouldCloseShape(pts, 3, 2, 2, 4.0) == 1);
    CHECK(stencil_shouldCloseShape(pts, 3, 50, 50, 4.0) == 0);
    CHECK(stencil_shouldCloseShape(pts, 2, 0, 0, 4.0) == 0);  // < 3 points
  }

  TEST_CASE("stencil_pageDimensions writes out the page size, swapping landscape") {
    double w = 0, h = 0;
    stencil_pageDimensions("A4", 100, 200, 0, 0, &w, &h);  // portrait image
    CHECK(w == doctest::Approx(21.0));
    CHECK(h == doctest::Approx(29.7));
    stencil_pageDimensions("A4", 200, 100, 0, 0, &w, &h);  // landscape -> swap
    CHECK(w == doctest::Approx(29.7));
    CHECK(h == doctest::Approx(21.0));
  }

  TEST_CASE("stencil_pixelToPageRaw scales per axis into the out pointers") {
    double x = 0, y = 0;
    stencil_pixelToPageRaw(50, 100, 21.0, 29.7, 100, 200, &x, &y);
    CHECK(x == doctest::Approx(10.5));    // 50/100 * 21
    CHECK(y == doctest::Approx(14.85));   // 100/200 * 29.7
  }

  TEST_CASE("stencil_formula* take the var name as a char code") {
    const int x = static_cast<int>('x');
    CHECK(stencil_formulaValidate("x + 1", x) == 1);
    CHECK(stencil_formulaValidate("x +", x) == 0);   // parse error
    CHECK(stencil_formulaValidate("", x) == 1);       // empty = identity

    CHECK(stencil_formulaApply("x * 2", x, 3.0, 1) == doctest::Approx(6.0));
    // allowFormulas = 0 -> value passes through unchanged.
    CHECK(stencil_formulaApply("x * 2", x, 3.0, 0) == doctest::Approx(3.0));

    double out = -1;
    CHECK(stencil_formulaEvaluate("x + 9", x, 3.0, &out) == 1);
    CHECK(out == doctest::Approx(12.0));
    // Division by zero -> non-finite -> failure, out left untouched.
    out = 77.0;
    CHECK(stencil_formulaEvaluate("x / 0", x, 3.0, &out) == 0);
    CHECK(out == doctest::Approx(77.0));
  }

  TEST_CASE("stencil_applyFilterRGBA filters an interleaved buffer in place") {
    std::uint8_t buf[] = {200, 0, 0, 10};  // one red pixel, alpha 10
    stencil_applyFilterRGBA(/*bw=*/1, buf, 1, 0, 0, 0);
    CHECK(buf[0] == 42);   // Rec.709 luma of pure red 200
    CHECK(buf[1] == 42);
    CHECK(buf[2] == 42);
    CHECK(buf[3] == 10);   // alpha preserved
    // mode 0 (none) is a no-op.
    std::uint8_t buf2[] = {1, 2, 3, 4};
    stencil_applyFilterRGBA(0, buf2, 1, 9, 9, 9);
    CHECK(buf2[0] == 1);
  }

  TEST_CASE("stencil_rotatePoints rotates a flat array in place") {
    double pts[] = {10, 0};
    stencil_rotatePoints(pts, 1, 0, 0, 3.14159265358979323846 / 2.0);
    CHECK(pts[0] == doctest::Approx(0.0).epsilon(0.0001));
    CHECK(pts[1] == doctest::Approx(10.0));
  }

  TEST_CASE("stencil_boundingBoxCenter writes the bbox center") {
    const double pts[] = {0, 0, 10, 0, 10, 10, 0, 10};
    double out[2] = {0, 0};
    stencil_boundingBoxCenter(pts, 4, out);
    CHECK(out[0] == doctest::Approx(5.0));
    CHECK(out[1] == doctest::Approx(5.0));
  }

  TEST_CASE("stencil_clampScale clamps into the zoom limits") {
    CHECK(stencil_clampScale(10.0) == doctest::Approx(5.0));
    CHECK(stencil_clampScale(0.01) == doctest::Approx(0.05));
    CHECK(stencil_clampScale(1.0) == doctest::Approx(1.0));
  }

  TEST_CASE("stencil_anchoredZoom fills out[scale, scrollLeft, scrollTop]") {
    double out[3] = {0, 0, 0};
    stencil_anchoredZoom(200, 100, 100, 50, 1.0, 2.0, out);
    CHECK(out[0] == doctest::Approx(2.0));
    CHECK(out[1] == doctest::Approx(500.0));
    CHECK(out[2] == doctest::Approx(250.0));
  }

  TEST_CASE("stencil_rectZoom fills out[scale, scrollLeft, scrollTop]") {
    double out[3] = {0, 0, 0};
    stencil_rectZoom(50, 50, 100, 100, 400, 200, out);
    CHECK(out[0] == doctest::Approx(2.0));
    CHECK(out[1] == doctest::Approx(0.0));
    CHECK(out[2] == doctest::Approx(100.0));
  }
}
