// The chat dock's slide clocks come from browser/js/config/motion.json (CHAT_SURFACE_*), the pair
// the browser's chat panel flies on: read, fallback and table agree.
#include "chatSlideClocks.hpp"

#include <QCoreApplication>
#include <cstdio>

#include "../../support/check.hpp"

namespace {
  int tableValue(const char* key) {
    QFile f(QStringLiteral(":/config/motion.json"));
    if (!f.open(QIODevice::ReadOnly)) return -1;
    return QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("ui")).toObject()
        .value(QLatin1String(key)).toInt(-1);
  }

  void pin(const char* key, int read, int fallback) {
    check(tableValue(key) == read, (QByteArray(key) + " is read from motion.json").constData());
    check(fallback == read, (QByteArray(key) + " falls back to the table's value").constData());
  }
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const stencil::gui::ChatSlideClocks& c = stencil::gui::chatSlideClocks();
  const stencil::gui::ChatSlideClocks f;
  pin("CHAT_SURFACE_IN_MS", c.inMs, f.inMs);
  pin("CHAT_SURFACE_OUT_MS", c.outMs, f.outMs);
  check(c.outMs < c.inMs, "leaving is the quicker half");
  check(stencil::gui::chatCloseMs() == std::lround(c.outMs / 1.5 * 1.3), "the desktop's docked chat close: /1.5, then x1.3");
  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
