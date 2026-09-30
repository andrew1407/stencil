#pragma once
// Interaction timings the desktop shares with common/config/constants.json (DEBOUNCE, POPOVER)
// and motion.json (FLIP_MS / FLIP_EASING) through the qrc, read once; the browser's popover.js,
// picker previews and ui/motion/flip.js read the same keys.
#include <QEasingCurve>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QStringList>

namespace stencil::support {

  // A build without the qrc; tests/support/uiTimings holds these equal to the table.
  struct UiTimings {
    int previewHoverMs = 280;   // DEBOUNCE.previewHoverMs: a rested hover previews
    int doubleClickMs = 250;    // POPOVER.doubleClickMs: a plain click waits this long for a second
    int pressSlopPx = 10;       // POPOVER.pressSlopPx: px a held press may drift
    int lingerCloseMs = 250;    // POPOVER.lingerCloseMs: a peek released inside outlasts a leave by this
  };

  inline const UiTimings& uiTimings() {
    static const UiTimings t = [] {
      UiTimings out;
      QFile f(QStringLiteral(":/config/constants.json"));
      if (!f.open(QIODevice::ReadOnly)) return out;
      const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
      auto num = [&root](const char* section, const char* key, int fallback) {
        const int v = root.value(QLatin1String(section)).toObject().value(QLatin1String(key)).toInt(0);
        return v > 0 ? v : fallback;
      };
      out.previewHoverMs = num("DEBOUNCE", "previewHoverMs", out.previewHoverMs);
      out.doubleClickMs = num("POPOVER", "doubleClickMs", out.doubleClickMs);
      out.pressSlopPx = num("POPOVER", "pressSlopPx", out.pressSlopPx);
      out.lingerCloseMs = num("POPOVER", "lingerCloseMs", out.lingerCloseMs);
      return out;
    }();
    return t;
  }

  // Qt's spline is the CSS curve exactly: x1 and x2 in [0, 1], the ends pinned at (0,0) and (1,1).
  inline QEasingCurve cubicBezier(double x1, double y1, double x2, double y2) {
    QEasingCurve c(QEasingCurve::BezierSpline);
    c.addCubicBezierSegment(QPointF(x1, y1), QPointF(x2, y2), QPointF(1, 1));
    return c;
  }

  // "cubic-bezier(x1, y1, x2, y2)" as that curve; `fallback` for any other text.
  inline QEasingCurve cssCubicBezier(const QString& css, const QEasingCurve& fallback) {
    const QString head = QStringLiteral("cubic-bezier(");
    const QString s = css.trimmed();
    if (!s.startsWith(head) || !s.endsWith(QLatin1Char(')'))) return fallback;
    const QStringList parts = s.mid(head.size(), s.size() - head.size() - 1).split(QLatin1Char(','));
    if (parts.size() != 4) return fallback;
    double v[4];
    for (int i = 0; i < 4; ++i) {
      bool ok = false;
      v[i] = parts[i].trimmed().toDouble(&ok);
      if (!ok) return fallback;
    }
    if (v[0] < 0 || v[0] > 1 || v[2] < 0 || v[2] > 1) return fallback;
    return cubicBezier(v[0], v[1], v[2], v[3]);
  }

  // A box that moved glides from where it was: motion.json ui FLIP_MS / FLIP_EASING.
  struct FlipMotion {
    int ms = 560;
    QEasingCurve easing = cubicBezier(0.16, 1, 0.22, 1);
  };

  inline const FlipMotion& flipMotion() {
    static const FlipMotion m = [] {
      FlipMotion out;
      QFile f(QStringLiteral(":/config/motion.json"));
      if (!f.open(QIODevice::ReadOnly)) return out;
      const QJsonObject ui = QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("ui")).toObject();
      const int ms = ui.value(QLatin1String("FLIP_MS")).toInt(0);
      if (ms > 0) out.ms = ms;
      out.easing = cssCubicBezier(ui.value(QLatin1String("FLIP_EASING")).toString(), out.easing);
      return out;
    }();
    return m;
  }

}  // namespace stencil::support
