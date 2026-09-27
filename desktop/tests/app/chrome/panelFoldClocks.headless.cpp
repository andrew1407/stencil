// The coordinate panel's fold clocks come from browser/js/config/motion.json (PANEL_*), the table
// the browser's panel/coordFold.js reads: read, fallback and table agree on the fold's clocks.
#include "PanelSlide.hpp"

#include <QCoreApplication>
#include <cstdio>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include "../../support/check.hpp"

namespace {
  int tableValue(const char* key) {
    QFile f(QStringLiteral(":/config/motion.json"));
    if (!f.open(QIODevice::ReadOnly)) return -1;
    return QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("ui")).toObject()
        .value(QLatin1String(key)).toInt(-1);
  }

  // One key: the table's value, the one the fold reads, the no-qrc fallback and the pinned clock agree.
  void pin(const char* key, int read, int fallback, int ms) {
    check(tableValue(key) == read, (QByteArray(key) + " is read from motion.json").constData());
    check(fallback == read, (QByteArray(key) + " falls back to the table's value").constData());
    check(read == ms, (QByteArray(key) + " is the pinned clock").constData());
  }
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const stencil::gui::PanelFoldClocks& c = stencil::gui::panelFoldClocks();
  const stencil::gui::PanelFoldClocks f;
  // The toolbar fold and the Controls pill spin ride the fold clocks; the panel's width slide the slide clocks.
  pin("PANEL_FOLD_IN_MS", c.slideInMs, f.slideInMs, 420);
  pin("PANEL_FOLD_OUT_MS", c.slideOutMs, f.slideOutMs, 630);
  pin("PANEL_SLIDE_IN_MS", c.panelInMs, f.panelInMs, 470);
  pin("PANEL_SLIDE_OUT_MS", c.panelOutMs, f.panelOutMs, 470);
  pin("PANEL_DUST_IN_MS", c.dustInMs, f.dustInMs, 450);
  pin("PANEL_DUST_OUT_MS", c.dustOutMs, f.dustOutMs, 390);
  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
