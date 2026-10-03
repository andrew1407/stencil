#pragma once
// The picture's quarter turn as numbers: its clock from common/config/motion.json (ROTATE_MS, ROTATE_EASING) and
// the frame at progress t. Browser twin: js/ui/motion/quarterTurn.js quarterTurnStart.
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QRegularExpression>

#include <array>

namespace stencil::gui::quarterTurn {

  struct Clock {
    int ms = 360;
    std::array<double, 4> bezier{0.22, 1, 0.36, 1};   // ROTATE_EASING's cubic-bezier control points
  };

  inline const Clock& clock() {
    static const Clock c = [] {
      Clock out;
      QFile f(QStringLiteral(":/config/motion.json"));
      if (!f.open(QIODevice::ReadOnly)) return out;
      const QJsonObject ui = QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("ui")).toObject();
      if (const int v = ui.value(QLatin1String("ROTATE_MS")).toInt(0); v > 0) out.ms = v;
      static const QRegularExpression re(QStringLiteral(R"(cubic-bezier\(([^,]+),([^,]+),([^,]+),([^)]+)\))"));
      const QRegularExpressionMatch m = re.match(ui.value(QLatin1String("ROTATE_EASING")).toString());
      if (m.hasMatch())
        for (int i = 0; i < 4; ++i) out.bezier[i] = m.captured(i + 1).trimmed().toDouble();
      return out;
    }();
    return c;
  }

  struct Frame {
    QPointF center;
    double degrees = 0;   // Qt's sense: positive is clockwise on screen
    double scale = 1;
  };

  // At t = 0 the new box `to` lies over `from` turned back a quarter (dir > 0 = CW); at t = 1 it rests.
  inline Frame frameAt(const QRectF& from, const QRectF& to, int dir, double t) {
    Frame f;
    const double s0 = to.height() > 0 ? from.width() / to.height() : 1;
    f.center = from.center() + (to.center() - from.center()) * t;
    f.degrees = (dir > 0 ? -90.0 : 90.0) * (1 - t);
    f.scale = s0 + (1 - s0) * t;
    return f;
  }

}  // namespace stencil::gui::quarterTurn
