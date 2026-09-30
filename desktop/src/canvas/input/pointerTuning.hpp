#pragma once
// The canvas's pointer tunings (common/config/constants.json HIT, HOLD_DRAW and
// DEBOUNCE.editCommitMs through the qrc), read once; the browser's pointer code and its hold ghost
// (renderer.js drawHoldPreview) read the same keys.
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>

namespace stencil::gui::pointerTuning {

  // A build without the qrc; tests/canvas/input/pointerTuning holds these equal to the table.
  struct Table {
    double lineRadiusPx = 8.0;        // screen px, over the zoom: findLineAt
    double pointRadiusPx = 10.0;      // screen px, over the zoom: the hover tooltip's point
    double grabRadiusPx = 12.0;       // screen px, over the zoom: findNearestPoint / Segment
    double closeSlackPx = 8.0;        // image px core::shouldCloseShape adds to the point size
    int editCommitMs = 280;           // a wheel burst's one undo step
    int holdTickMs = 40;
    int holdDelayMs = 500;
    double holdMoveTolerancePx = 6.0;
    double holdRearmDistancePx = 10.0;
    QList<qreal> holdGhostDashPx{6.0, 4.0};   // image px, whatever the pen's width
    double holdGhostLineAlpha = 0.45;
    double holdGhostPointAlpha = 0.6;
  };

  inline const Table& table() {
    static const Table t = [] {
      Table out;
      QFile f(QStringLiteral(":/config/constants.json"));
      if (!f.open(QIODevice::ReadOnly)) return out;
      const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
      auto num = [&root](const char* section, const char* key, double fallback) {
        const double v = root.value(QLatin1String(section)).toObject().value(QLatin1String(key)).toDouble(0);
        return v > 0 ? v : fallback;
      };
      out.lineRadiusPx = num("HIT", "lineRadiusPx", out.lineRadiusPx);
      out.pointRadiusPx = num("HIT", "pointRadiusPx", out.pointRadiusPx);
      out.grabRadiusPx = num("HIT", "grabRadiusPx", out.grabRadiusPx);
      out.closeSlackPx = num("HIT", "closeSlackPx", out.closeSlackPx);
      out.editCommitMs = int(num("DEBOUNCE", "editCommitMs", out.editCommitMs));
      out.holdTickMs = int(num("HOLD_DRAW", "tickMs", out.holdTickMs));
      out.holdDelayMs = int(num("HOLD_DRAW", "delayMs", out.holdDelayMs));
      out.holdMoveTolerancePx = num("HOLD_DRAW", "moveTolerancePx", out.holdMoveTolerancePx);
      out.holdRearmDistancePx = num("HOLD_DRAW", "rearmDistancePx", out.holdRearmDistancePx);
      out.holdGhostLineAlpha = num("HOLD_DRAW", "ghostLineAlpha", out.holdGhostLineAlpha);
      out.holdGhostPointAlpha = num("HOLD_DRAW", "ghostPointAlpha", out.holdGhostPointAlpha);
      const QJsonArray dash = root.value(QLatin1String("HOLD_DRAW")).toObject().value(QLatin1String("ghostDashPx")).toArray();
      if (!dash.isEmpty()) {
        out.holdGhostDashPx.clear();
        for (const QJsonValue& v : dash) out.holdGhostDashPx << v.toDouble();
      }
      return out;
    }();
    return t;
  }

}  // namespace stencil::gui::pointerTuning
