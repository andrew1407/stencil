// The image rotate's quarter turn (canvas/overlay/quarterTurn.hpp): its clock is motion.json's
// ROTATE_MS and ROTATE_EASING, and the frame starts over the old box turned back a quarter and ends at rest.
#include "quarterTurn.hpp"

#include <QCoreApplication>
#include <cmath>
#include <cstdio>

#include "../support/check.hpp"

namespace qt = stencil::gui::quarterTurn;

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QFile f(QStringLiteral(":/config/motion.json"));
  check(f.open(QIODevice::ReadOnly), "motion.json is in the qrc");
  const QJsonObject ui = QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("ui")).toObject();
  check(qt::clock().ms == ui.value(QLatin1String("ROTATE_MS")).toInt(-1), "ROTATE_MS is read from motion.json");
  const auto& cb = qt::clock().bezier;
  const QString easing = QStringLiteral("cubic-bezier(%1, %2, %3, %4)").arg(cb[0]).arg(cb[1]).arg(cb[2]).arg(cb[3]);
  check(easing == ui.value(QLatin1String("ROTATE_EASING")).toString(), "ROTATE_EASING's control points are read");

  const QRectF from(0, 0, 400, 200), to(150, 0, 100, 200);
  const qt::Frame a = qt::frameAt(from, to, 1, 0);
  check(a.degrees == -90 && a.scale == 2 && a.center == from.center(), "CW starts a quarter back over the old box");
  check(qt::frameAt(from, to, -1, 0).degrees == 90, "CCW starts the other way");
  const qt::Frame b = qt::frameAt(from, to, 1, 1);
  check(b.degrees == 0 && std::abs(b.scale - 1) < 1e-9 && b.center == to.center(), "it ends upright on the new box");
  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
