// The keyword chips' clocks come from browser/js/config/motion.json the way the browser's keyword
// chips read them: the cloud and the leave on CHIP_DUST_MS, the fade up held CHIP_ENTER_DELAY_MS and
// then run over CHIP_ENTER_MS; read, fallback and table agree, and the fade's curve holds, then rises.
#include "chipClocks.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <cstdio>

#include "../../../support/check.hpp"

namespace {
  double tableValue(const char* key) {
    QFile f(QStringLiteral(":/config/motion.json"));
    if (!f.open(QIODevice::ReadOnly)) return -1;
    return QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("ui")).toObject()
        .value(QLatin1String(key)).toDouble(-1);
  }

  void pin(const char* key, double read, double fallback, const char* what) {
    check(tableValue(key) > 0 && tableValue(key) == read, what);
    check(fallback == read, (QByteArray(key) + " falls back to the table's value").constData());
  }

  // CSS cubic-bezier(0, 0, 0.6, 1) at x: x(u) solved by bisection, then y(u).
  double rowFilterIn(double x) {
    const auto bez = [](double a, double b, double u) {
      return 3 * a * u * (1 - u) * (1 - u) + 3 * b * u * u * (1 - u) + u * u * u;
    };
    double lo = 0, hi = 1;
    for (int i = 0; i < 60; ++i) {
      const double mid = (lo + hi) / 2;
      if (bez(0.0, 0.6, mid) < x) lo = mid;
      else hi = mid;
    }
    return bez(0.0, 1.0, (lo + hi) / 2);
  }
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const stencil::gui::KeywordChipClocks& c = stencil::gui::keywordChipClocks();
  const stencil::gui::KeywordChipClocks f;
  pin("CHIP_DUST_MS", c.dustMs, f.dustMs, "a chip's cloud flies CHIP_DUST_MS both ways");
  pin("CHIP_DUST_MS", c.leaveMs, f.leaveMs, "…and it leaves on CHIP_DUST_MS, not CHIP_LEAVE_MS");
  pin("CHIP_ENTER_DELAY_MS", c.enterDelayMs, f.enterDelayMs, "the fade up waits CHIP_ENTER_DELAY_MS");
  pin("CHIP_ENTER_MS", c.enterMs, f.enterMs, "…then runs CHIP_ENTER_MS");
  pin("CHIP_DUST_DRIFT", c.dustDrift, f.dustDrift, "the cloud throws CHIP_DUST_DRIFT");

  const QEasingCurve held = stencil::gui::chipEnterCurve(c.enterDelayMs, c.enterMs);
  const double h = double(c.enterDelayMs) / (c.enterDelayMs + c.enterMs);
  check(held.valueForProgress(0.0) == 0.0 && std::abs(held.valueForProgress(h * 0.5)) < 1e-9 &&
            std::abs(held.valueForProgress(h - 1e-3)) < 1e-9,
        "the chip stays hidden through the delay");
  check(std::abs(held.valueForProgress(1.0) - 1.0) < 1e-9, "…and lands fully shown");
  bool follows = true;
  for (double x : {0.1, 0.25, 0.5, 0.75, 0.9})
    follows = follows && std::abs(held.valueForProgress(h + x * (1 - h)) - rowFilterIn(x)) < 1e-3;
  check(follows, "…rising on rowFilterIn's cubic-bezier(0, 0, 0.6, 1) over the rest");
  const QEasingCurve now = stencil::gui::chipEnterCurve(0, c.enterMs);
  check(std::abs(now.valueForProgress(0.5) - rowFilterIn(0.5)) < 1e-3,
        "with no cloud to wait for, the rise starts at once");
  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
