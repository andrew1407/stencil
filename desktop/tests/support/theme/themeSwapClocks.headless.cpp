// The theme wipe's numbers come from browser/js/config/motion.json, the table the browser's
// themeSwap.js and swapDust.js read: the two clocks through the qrc, the rest held equal here.
#include "ThemeSwapOverlay.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <cstdio>

#include "../../support/check.hpp"

using stencil::gui::ThemeSwapOverlay;

namespace {
  QJsonObject motionUi() {
    QFile f(QStringLiteral(":/config/motion.json"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("ui")).toObject();
  }

  void same(const QJsonObject& ui, const char* key, double desktop) {
    const double table = ui.value(QLatin1String(key)).toDouble(-1);
    check(std::abs(table - desktop) < 1e-9, (QByteArray(key) + " matches motion.json").constData());
  }
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const QJsonObject ui = motionUi();
  check(!ui.isEmpty(), "motion.json is in the qrc");
  same(ui, "THEME_SWAP_MS", ThemeSwapOverlay::swapMs());
  same(ui, "SWAP_DUST_LIFE_MS", ThemeSwapOverlay::dustLifeMs());
  // The pre-table literals stay the no-qrc fallback.
  check(ThemeSwapOverlay::swapMs() == 280 && ThemeSwapOverlay::dustLifeMs() == 340, "the pinned clocks");
  same(ui, "SWAP_EDGE_POINTS", ThemeSwapOverlay::EDGE_POINTS);
  same(ui, "SWAP_DUST_MOTES", ThemeSwapOverlay::DUST_MOTES);
  same(ui, "SWAP_DUST_MIN_T", ThemeSwapOverlay::DUST_MIN_T);
  same(ui, "SWAP_DUST_MAX_T", ThemeSwapOverlay::DUST_MAX_T);
  same(ui, "SWAP_DUST_STEPS", ThemeSwapOverlay::GRAIN_STEPS);
  same(ui, "SWAP_DUST_FLARE", ThemeSwapOverlay::GRAIN_FLARE);
  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
