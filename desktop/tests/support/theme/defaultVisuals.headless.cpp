// The drawing and highlight defaults (support/theme/defaultVisuals.hpp) are read from
// common/config/constants.json DEFAULT_VISUALS and HOLD_DRAW.delayMs: read, fallback and table
// agree, and a fresh Settings starts from them.
#include "defaultVisuals.hpp"
#include "fileStore.hpp"

#include <QCoreApplication>
#include <cstdio>

#include "../check.hpp"

namespace {
  QJsonValue tableValue(const char* key) {
    QFile f(QStringLiteral(":/config/constants.json"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("DEFAULT_VISUALS"))
        .toObject().value(QLatin1String(key));
  }

  void pin(const char* key, const QString& read, const QString& fallback) {
    const QByteArray what = QByteArray("DEFAULT_VISUALS.") + key;
    check(tableValue(key).toString() == read, (what + " is read from constants.json").constData());
    check(fallback == read, (what + " falls back to the table's value").constData());
  }

  void pin(const char* key, double read, double fallback) {
    const QByteArray what = QByteArray("DEFAULT_VISUALS.") + key;
    check(tableValue(key).toDouble(-1) == read, (what + " is read from constants.json").constData());
    check(fallback == read, (what + " falls back to the table's value").constData());
  }
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  namespace dv = stencil::gui::defaultVisuals;
  const dv::Table& t = dv::table();
  const dv::Table f;
  pin("color", t.color, f.color);
  pin("thickness", t.thickness, f.thickness);
  pin("pointSize", t.pointSize, f.pointSize);
  pin("style", t.style, f.style);
  pin("defaultFillColor", t.fillColor, f.fillColor);
  pin("selGlowColor", t.selGlow, f.selGlow);
  pin("hoverRingColor", t.hoverRing, f.hoverRing);
  pin("focusRingColor", t.focusRing, f.focusRing);
  QFile cf(QStringLiteral(":/config/constants.json"));
  const int holdMs = cf.open(QIODevice::ReadOnly)
      ? QJsonDocument::fromJson(cf.readAll()).object().value(QLatin1String("HOLD_DRAW")).toObject()
            .value(QLatin1String("delayMs")).toInt(-1)
      : -1;
  check(holdMs == t.holdDrawDelay, "HOLD_DRAW.delayMs is read from constants.json");
  check(f.holdDrawDelay == t.holdDrawDelay, "HOLD_DRAW.delayMs falls back to the table's value");
  const stencil::gui::Settings s;
  check(s.defaultColor == t.color && s.defaultThickness == t.thickness && s.defaultPointSize == t.pointSize
            && s.defaultStyle == t.style && s.defaultFillColor == t.fillColor && s.selGlowColor == t.selGlow
            && s.hoverRingColor == t.hoverRing && s.focusRingColor == t.focusRing
            && s.holdDrawDelay == t.holdDrawDelay,
        "a fresh Settings starts from DEFAULT_VISUALS and HOLD_DRAW.delayMs");
  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
