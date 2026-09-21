// Headless check of app/UnitsController.hpp — the display-unit and px→cm arithmetic every length
// readout in the window shares. The browser twin is js/core/settings/units.js layoutLineLengthCm, so the rule
// pinned here is the one that has to match it: measure off the RAW per-axis page scale, never the
// formula path. Pure arithmetic; no widgets, no display.
#include "UnitsController.hpp"

#include <QCoreApplication>
#include <cmath>

#include "support/check.hpp"

using stencil::gui::UnitsController;

static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  // ── The two codes, and what an unknown one falls back to.
  check(UnitsController::isInches(QStringLiteral("in")), "\"in\" is inches");
  check(!UnitsController::isInches(QStringLiteral("cm")), "\"cm\" is not");
  check(!UnitsController::isInches(QString()), "an unset unit is not inches");
  check(UnitsController::canonicalUnit(QStringLiteral("in")) == QStringLiteral("in"),
        "inches canonicalises to itself");
  check(UnitsController::canonicalUnit(QStringLiteral("cm")) == QStringLiteral("cm"),
        "…and so does centimetres");
  check(UnitsController::canonicalUnit(QStringLiteral("mm")) == QStringLiteral("cm"),
        "anything else reads as centimetres, never as a third unit");
  check(UnitsController::canonicalUnit(QStringLiteral("IN")) == QStringLiteral("cm"),
        "the comparison is exact, so a stored \"IN\" is not silently accepted");

  // ── Model values are centimetres; the factor is what the display multiplies by.
  check(near(UnitsController::factor(QStringLiteral("cm")), 1.0), "centimetres are the model unit");
  check(near(UnitsController::factor(QStringLiteral("in")), 1.0 / 2.54),
        "inches scale by exactly 1/2.54");
  check(near(UnitsController::factor(QStringLiteral("cm")) * 2.54 *
                 UnitsController::factor(QStringLiteral("in")),
             1.0),
        "one inch is 2.54 cm, round trip");
  check(QLatin1String(UnitsController::label(QStringLiteral("in"))) == QLatin1String("in"),
        "the inch label is the one the status bar prints");
  check(QLatin1String(UnitsController::label(QStringLiteral("cm"))) == QLatin1String("cm"),
        "…and so is the centimetre one");

  // ── Inches need the extra digit to carry what one centimetre decimal does.
  check(UnitsController::decimalsFor(QStringLiteral("cm")) == 1, "centimetres show one decimal");
  check(UnitsController::decimalsFor(QStringLiteral("in")) == 2, "inches show two");

  // ── The px→cm scale: per axis, from the page box and the image size.
  {
    const auto s = UnitsController::pxToCm(21.0, 29.7, 2100, 2970);   // A4 at 100 px/cm
    check(s.measurable(), "a real page and a real image are measurable");
    check(near(s.x, 0.01) && near(s.y, 0.01), "100 px is one millimetre on an A4 at 2100x2970");
  }
  {
    // A non-square pixel: the axes scale independently, which is exactly why a length is
    // not just "pixels times one number".
    const auto s = UnitsController::pxToCm(20.0, 10.0, 100, 100);
    check(near(s.x, 0.2) && near(s.y, 0.1), "a wide page stretches x twice as far as y");
  }
  {
    check(!UnitsController::pxToCm(21.0, 29.7, 0, 100).measurable(), "no image width, nothing to measure");
    check(!UnitsController::pxToCm(21.0, 29.7, 100, 0).measurable(), "nor with no height");
    check(!UnitsController::pxToCm(0.0, 29.7, 100, 100).measurable(), "nor on a zero-width page");
    check(!UnitsController::pxToCm(21.0, -1.0, 100, 100).measurable(),
          "a negative page dimension is refused rather than producing a negative length");
    check(!UnitsController::pxToCm(21.0, 29.7, -5, 100).measurable(), "…and so is a negative size");
  }

  // ── Segment lengths, measured through that scale.
  {
    const auto s = UnitsController::pxToCm(20.0, 10.0, 100, 100);   // 0.2 cm/px by 0.1 cm/px
    check(near(UnitsController::segmentCm(10, 0, s), 2.0), "ten pixels across is 2 cm");
    check(near(UnitsController::segmentCm(0, 10, s), 1.0), "ten pixels down is 1 cm");
    check(near(UnitsController::segmentCm(-10, 0, s), 2.0), "direction does not shorten a segment");
    check(near(UnitsController::segmentCm(0, 0, s), 0.0), "a point has no length");
    // The diagonal is the hypotenuse of the two SCALED legs, not of the raw pixels.
    check(near(UnitsController::segmentCm(10, 10, s), std::hypot(2.0, 1.0)),
          "the diagonal uses the scaled legs, so a stretched page does not distort it");
  }
  {
    // A square page and a square image: the classic 3-4-5, in centimetres.
    const auto s = UnitsController::pxToCm(100.0, 100.0, 100, 100);
    check(near(UnitsController::segmentCm(3, 4, s), 5.0), "3-4-5 at one cm per pixel");
    // Summing a polyline is the caller's loop; it must be scale-invariant the same way.
    const double a = UnitsController::segmentCm(3, 4, s) + UnitsController::segmentCm(4, 3, s);
    check(near(a, 10.0), "two legs add up, which is how a drawn line is totalled");
  }

  // ── The controller starts owning no widgets; the toolbar fills them in.
  {
    UnitsController u;
    check(!u.pageSize && !u.customGroup && !u.customW && !u.customH, "no page controls yet");
    check(!u.unitCombo && !u.unitCm && !u.unitIn, "and no unit switch in either of its places");
  }

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}
