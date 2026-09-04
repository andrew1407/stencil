#include "cssColor.hpp"
#include <QCoreApplication>
#include <cstdio>
#include "support/check.hpp"
using stencil::gui::cssColor;
using stencil::gui::cssName;
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const QColor solid = cssColor(QStringLiteral("#ff8800"));
  check(solid.isValid() && solid.red() == 255 && solid.green() == 136 && solid.blue() == 0 &&
        solid.alpha() == 255, "a plain #rrggbb still parses opaque");
  const QColor a = cssColor(QStringLiteral("#ff880080"));
  check(a.isValid() && a.red() == 255 && a.green() == 136 && a.blue() == 0 && a.alpha() == 128,
        "#rrggbbaa carries its alpha (CSS order, not Qt's #AARRGGBB)");
  check(cssName(solid) == QStringLiteral("#ff8800"), "opaque writes back as #rrggbb");
  check(cssName(a) == QStringLiteral("#ff880080"), "translucent writes back as #rrggbbaa");
  check(cssName(cssColor(cssName(a))) == cssName(a), "…and round-trips");
  check(cssColor(QStringLiteral("red")).isValid(), "named colours still parse");
  check(!cssColor(QStringLiteral("#zzzzzzzz")).isValid(), "garbage is invalid, not silently black");
  check(!cssName(QColor()).size(), "an invalid colour names nothing");
  std::puts("cssColor: OK");
  return 0;
}
