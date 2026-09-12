// Native coverage for the crop-geometry half of the WebAssembly ABI
// (core/wasmCropApi.cpp), split from wasmApi.test.cpp on size. Same contract: the
// wrappers are thin, so this guards the marshalling (output pointers, the baked-in
// minSize default) rather than the already-tested cropGeometry math.
#include "doctest.h"

extern "C" {
  int stencil_isAlbumOrientation(double, double);
  double stencil_cropAspect(double, double, int);
  void stencil_centeredCrop(double, double, double, double*);
  void stencil_resizeCropFromCorner(double, double, double, double, int, double,
                                    double, double, double, double, double,
                                    double*);
  void stencil_moveCropClamped(double, double, double, double, double, double,
                               double, double, double*);
  void stencil_scaleCropCentered(double, double, double, double, double, double,
                                 double, double, double*);
  double stencil_cropResizeScale(double, double);
  void stencil_cropChange(double, double, double, double, double, double, double,
                          double, double*);
  void stencil_rotateCropRectQuarter(double, double, double, double, double,
                                     double, int, double*);
}

TEST_SUITE("wasmApi") {
  TEST_CASE("stencil_isAlbumOrientation / cropAspect forward to the core") {
    CHECK(stencil_isAlbumOrientation(200, 100) == 1);
    CHECK(stencil_isAlbumOrientation(100, 200) == 0);
    CHECK(stencil_cropAspect(29.7, 42.0, 0) == doctest::Approx(29.7 / 42.0));
    CHECK(stencil_cropAspect(29.7, 42.0, 1) == doctest::Approx(42.0 / 29.7));
  }

  TEST_CASE("stencil_centeredCrop writes {x,y,width,height}") {
    double out[4] = {0, 0, 0, 0};
    // 100x200 portrait at A3 aspect → 100x141.4, centered vertically.
    stencil_centeredCrop(100, 200, 29.7 / 42.0, out);
    CHECK(out[0] == doctest::Approx(0.0));               // x
    CHECK(out[2] == doctest::Approx(100.0));             // width
    CHECK(out[3] == doctest::Approx(100.0 * 42.0 / 29.7));  // height ≈ 141.4
    CHECK(out[1] == doctest::Approx((200.0 - out[3]) / 2.0));  // y centered
  }

  TEST_CASE("stencil_resizeCropFromCorner keeps aspect, clamps to bounds") {
    double out[4] = {0, 0, 0, 0};
    const double aspect = 42.0 / 29.7;  // album
    stencil_resizeCropFromCorner(10, 10, 100, 100 / aspect, 2, 5000, 5000, aspect,
                                 200, 200, 16, out);
    CHECK(out[2] / out[3] == doctest::Approx(aspect));   // aspect preserved
    CHECK(out[0] + out[2] <= doctest::Approx(200.0));    // within image
    CHECK(out[1] + out[3] <= doctest::Approx(200.0));
  }

  TEST_CASE("stencil_moveCropClamped clamps inside the image") {
    double out[4] = {0, 0, 0, 0};
    stencil_moveCropClamped(10, 10, 100, 80, 9999, 0, 500, 500, out);
    CHECK(out[0] == doctest::Approx(400.0));  // imageW - width
    CHECK(out[1] == doctest::Approx(10.0));
  }

  TEST_CASE("stencil_scaleCropCentered scales about the centre, caps and floors") {
    double out[4] = {0, 0, 0, 0};
    // Grow 1.5x about centre (100,100) in a 200x200 image: 80 -> 120, centre held.
    stencil_scaleCropCentered(60, 60, 80, 80, 1.5, 1.0, 200, 200, out);
    CHECK(out[2] == doctest::Approx(120.0));
    CHECK(out[3] == doctest::Approx(120.0));
    CHECK(out[0] + out[2] / 2.0 == doctest::Approx(100.0));  // centre x unchanged
    CHECK(out[1] + out[3] / 2.0 == doctest::Approx(100.0));
    // Over-grow: capped by the nearer edge to fill the image, repositioned in-bounds.
    stencil_scaleCropCentered(60, 60, 80, 80, 100.0, 1.0, 200, 200, out);
    CHECK(out[2] == doctest::Approx(200.0));
    CHECK(out[0] == doctest::Approx(0.0));
    CHECK(out[1] == doctest::Approx(0.0));
    // Shrink hard: floored at the wrapper's baked-in default minSize (16).
    stencil_scaleCropCentered(60, 60, 80, 80, 0.0001, 1.0, 200, 200, out);
    CHECK(out[2] == doctest::Approx(16.0));
    CHECK(out[3] == doctest::Approx(16.0));
  }

  TEST_CASE("stencil_cropResizeScale / cropChange report rescale vs flip") {
    CHECK(stencil_cropResizeScale(100, 250) == doctest::Approx(2.5));
    double out[2] = {0, 0};
    stencil_cropChange(0, 0, 100, 141, 0, 0, 200, 282, out);  // resized
    CHECK(out[0] == doctest::Approx(0.0));   // orientation unchanged
    CHECK(out[1] == doctest::Approx(2.0));   // scale
    stencil_cropChange(0, 0, 100, 141, 0, 0, 141, 100, out);  // flipped
    CHECK(out[0] == doctest::Approx(1.0));   // orientation changed
    CHECK(out[1] == doctest::Approx(1.0));
  }

  TEST_CASE("stencil_rotateCropRectQuarter writes the turned {x,y,width,height}") {
    double out[4] = {0, 0, 0, 0};
    stencil_rotateCropRectQuarter(10, 20, 80, 40, 200, 100, 1, out);  // CW
    CHECK(out[0] == doctest::Approx(100 - (20 + 40)));
    CHECK(out[1] == doctest::Approx(10));
    CHECK(out[2] == doctest::Approx(40));
    CHECK(out[3] == doctest::Approx(80));
    stencil_rotateCropRectQuarter(10, 20, 80, 40, 200, 100, 0, out);  // CCW
    CHECK(out[0] == doctest::Approx(20));
    CHECK(out[1] == doctest::Approx(200 - (10 + 80)));
    CHECK(out[2] == doctest::Approx(40));
    CHECK(out[3] == doctest::Approx(80));
  }
}
