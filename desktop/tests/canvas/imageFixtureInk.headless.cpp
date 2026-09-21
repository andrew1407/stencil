// On-accent ink (WCAG contrast, the browser's accents.js needsDarkGlyph rule) and the arithmetic a
// numeric field takes, whose cases mirror browser/tests/numericInput.test.js.
#include "numericInput.hpp"
#include "theme.hpp"

#include <QColor>
#include <QString>
#include <cstdio>

#include "../support/check.hpp"

void accentInkAndNumericInput() {
  // On-accent ink: the accent picks whichever of white / near-black contrasts more (WCAG), the same rule
  // as the browser (accents.js needsDarkGlyph) and the extension (lib/accent.js).
  std::printf("on-accent ink:\n");
  using stencil::gui::accentNeedsDarkGlyph;
  using stencil::gui::onAccentInk;
  check(!accentNeedsDarkGlyph(QColor("#7c3aed")), "violet default keeps white (5.70 vs 3.69)");
  check(accentNeedsDarkGlyph(QColor("#eab308")), "yellow flips to the dark ink (1.92 vs 10.95)");
  check(accentNeedsDarkGlyph(QColor("#0ea5e9")), "so does sky (2.77 vs 7.58)");
  check(accentNeedsDarkGlyph(QColor("#00ffff")), "and a light custom accent");
  check(!accentNeedsDarkGlyph(QColor("#000000")), "black keeps white (21:1)");
  check(!accentNeedsDarkGlyph(QColor()), "an invalid colour keeps the white default");
  check(onAccentInk(QColor("#eab308")) == QColor("#1a1a1a"), "the dark ink is the page ink");
  check(onAccentInk(QColor("#7c3aed")) == QColor(Qt::white), "…and the light one is white");
  QString flagged;
  for (const auto& a : stencil::gui::accentPresets())
    if (accentNeedsDarkGlyph(QColor(a.hex))) flagged += a.key + QLatin1String(" ");
  check(flagged == "pink orange brown yellow grass turquoise aqua sky bluegray ",
        "exactly the light presets flip, in palette order (browser/extension parity)");

  // The palette hands the ink out with the theme, tick image included.
  check(stencil::gui::themePalette(false, "yellow").onAccent == QColor("#1a1a1a"),
        "themePalette carries the accent's ink");
  check(stencil::gui::buildStylesheet(false, "yellow").contains(":/icons/check-dark.png"),
        "a light accent's checkbox takes the dark tick");
  check(stencil::gui::buildStylesheet(false, "violet").contains(":/icons/check.png"),
        "…and a dark accent keeps the white one");

  // Numeric fields take an arithmetic expression (support/numericInput.cpp): the cases mirror
  // browser/tests/numericInput.test.js, on core/parse/formulaParser's operator set, so all three agree.
  std::printf("numeric input expressions:\n");
  auto ev = [](const char* text, double current, double* out) {
    bool ok = false;
    const double v = stencil::gui::evalNumericExpression(QString::fromUtf8(text), current, &ok);
    if (out) *out = v;
    return ok;
  };
  double v = 0;
  check(ev("54", 0, &v) && v == 54, "a plain number passes through");
  check(ev("45 + 9", 0, &v) && v == 54, "\"45 + 9\" evaluates to 54");
  check(ev("45+9", 0, &v) && v == 54, "…with or without spaces");
  check(ev("* 9", 3, &v) && v == 27, "a leading * continues from the current value");
  check(ev("/2", 10, &v) && v == 5, "…and so does a leading /");
  check(ev("-5", 10, &v) && v == -5, "a leading - stays a SIGN, not a subtraction");
  check(ev("2 + 3 * 4", 0, &v) && v == 14, "* binds tighter than +");
  check(ev("(2 + 3) * 4", 0, &v) && v == 20, "parentheses group");
  check(ev("2 ** 3 ** 2", 0, &v) && v == 512, "** is right-associative");
  check(ev("-2 ** 2", 0, &v) && v == -4, "unary sign applies outside ** (core parity)");
  check(!ev("", 0, nullptr), "empty text is not a value");
  check(!ev("abc", 0, nullptr), "letters are not a value");
  check(!ev("45 +", 0, nullptr), "a dangling operator is not a value");
  check(!ev("1/0", 0, nullptr), "division by zero is rejected, not infinite");
  check(!ev("1 2", 0, nullptr), "trailing junk is rejected");

}
