#pragma once
// The keyword chips' clocks, ms: common/config/motion.json's CHIP_DUST_MS / CHIP_DUST_DRIFT, the
// chip cloud both ways and the leave (keywordChips.js KEYWORD_LEAVE_MS = CHIP_DUST_MS), and
// CHIP_ENTER_DELAY_MS / CHIP_ENTER_MS, a new chip's fade up; read once,
// tests/dialogs/meta/keywords/chipClocks holds them.
#include <QEasingCurve>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointF>

namespace stencil::gui {

  struct KeywordChipClocks {
    int dustMs = 630;          // one chip cloud's flight: the gather in, the scatter out
    int leaveMs = 630;         // the slot held, then its width's collapse
    int enterDelayMs = 285;    // a new chip stays hidden while its cloud lands…
    int enterMs = 510;         // …then fades up
    double dustDrift = 0.15;   // the cloud's throw, as a share of a list row's
  };

  inline const KeywordChipClocks& keywordChipClocks() {
    static const KeywordChipClocks c = [] {
      KeywordChipClocks out;
      QFile f(QStringLiteral(":/config/motion.json"));
      if (!f.open(QIODevice::ReadOnly)) return out;
      const QJsonObject ui = QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("ui")).toObject();
      const auto ms = [&ui](const char* key, int fallback) {
        const int v = ui.value(QLatin1String(key)).toInt(0);
        return v > 0 ? v : fallback;
      };
      out.dustMs = out.leaveMs = ms("CHIP_DUST_MS", out.dustMs);
      out.enterDelayMs = ms("CHIP_ENTER_DELAY_MS", out.enterDelayMs);
      out.enterMs = ms("CHIP_ENTER_MS", out.enterMs);
      const double drift = ui.value(QLatin1String("CHIP_DUST_DRIFT")).toDouble(0);
      if (drift > 0) out.dustDrift = drift;
      return out;
    }();
    return c;
  }

  // The fade over holdMs + riseMs: 0 through the hold, then rowFilterIn's cubic-bezier(0, 0, 0.6, 1)
  // (browser css/animations/reveal/groups.css under meta.css's .kw-chip.filter-entering).
  inline QEasingCurve chipEnterCurve(int holdMs, int riseMs) {
    QEasingCurve c(QEasingCurve::BezierSpline);
    const double h = holdMs > 0 && riseMs > 0 ? double(holdMs) / (holdMs + riseMs) : 0.0;
    if (h > 0) c.addCubicBezierSegment(QPointF(h / 3, 0), QPointF(h * 2 / 3, 0), QPointF(h, 0));
    c.addCubicBezierSegment(QPointF(h, 0), QPointF(h + 0.6 * (1 - h), 1), QPointF(1, 1));
    return c;
  }

}  // namespace stencil::gui
