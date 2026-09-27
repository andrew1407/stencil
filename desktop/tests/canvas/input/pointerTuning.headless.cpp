// The canvas's pointer tunings come from constants.json (HIT, HOLD_DRAW, DEBOUNCE.editCommitMs)
// with the values the desktop always used, the hold ghost's dash and alphas with the browser's, and
// the hit tests measure those radii on screen.
#include "CanvasWidget.hpp"
#include "pointerTuning.hpp"

#include <QApplication>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "../../support/check.hpp"

using stencil::gui::CanvasWidget;
namespace tuning = stencil::gui::pointerTuning;

namespace {
  double tableValue(const char* section, const char* key) {
    QFile f(QStringLiteral(":/config/constants.json"));
    if (!f.open(QIODevice::ReadOnly)) return -1;
    return QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String(section)).toObject()
        .value(QLatin1String(key)).toDouble(-1);
  }

  // One key: the table's value, the one the canvas reads, the no-qrc fallback and the former literal agree.
  void pin(const char* section, const char* key, double read, double fallback, double literal) {
    const QByteArray what = QByteArray(section) + "." + key;
    check(tableValue(section, key) == read, (what + " is read from constants.json").constData());
    check(fallback == read, (what + " falls back to the table's value").constData());
    check(read == literal, (what + " keeps the desktop's value").constData());
  }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  const tuning::Table& t = tuning::table();
  const tuning::Table f;

  pin("HIT", "lineRadiusPx", t.lineRadiusPx, f.lineRadiusPx, 8);
  pin("HIT", "pointRadiusPx", t.pointRadiusPx, f.pointRadiusPx, 10);
  pin("HIT", "grabRadiusPx", t.grabRadiusPx, f.grabRadiusPx, 12);
  pin("HIT", "closeSlackPx", t.closeSlackPx, f.closeSlackPx, 8);
  pin("DEBOUNCE", "editCommitMs", t.editCommitMs, f.editCommitMs, 280);
  pin("HOLD_DRAW", "tickMs", t.holdTickMs, f.holdTickMs, 40);
  pin("HOLD_DRAW", "delayMs", t.holdDelayMs, f.holdDelayMs, 500);
  pin("HOLD_DRAW", "moveTolerancePx", t.holdMoveTolerancePx, f.holdMoveTolerancePx, 6);
  pin("HOLD_DRAW", "rearmDistancePx", t.holdRearmDistancePx, f.holdRearmDistancePx, 10);
  pin("HOLD_DRAW", "ghostLineAlpha", t.holdGhostLineAlpha, f.holdGhostLineAlpha, 0.45);
  pin("HOLD_DRAW", "ghostPointAlpha", t.holdGhostPointAlpha, f.holdGhostPointAlpha, 0.6);
  {
    QFile c(QStringLiteral(":/config/constants.json"));
    QList<qreal> table;
    if (c.open(QIODevice::ReadOnly))
      for (const QJsonValue& v : QJsonDocument::fromJson(c.readAll()).object().value(QLatin1String("HOLD_DRAW"))
                                     .toObject().value(QLatin1String("ghostDashPx")).toArray())
        table << v.toDouble();
    check(!table.isEmpty() && table == t.holdGhostDashPx, "HOLD_DRAW.ghostDashPx is read from constants.json");
    check(f.holdGhostDashPx == t.holdGhostDashPx, "HOLD_DRAW.ghostDashPx falls back to the table's value");
    check(t.holdGhostDashPx == QList<qreal>({6.0, 4.0}), "HOLD_DRAW.ghostDashPx keeps the browser's [6, 4] px");
  }

  CanvasWidget canvas;
  check(canvas.holdDrawDelay() == t.holdDelayMs, "the hold-to-draw delay starts at HOLD_DRAW.delayMs");
  QImage page(400, 200, QImage::Format_RGB32);
  page.fill(Qt::white);
  canvas.loadFromImage(page, stencil::core::CropRect{0, 0, 400, 200}, 0);
  stencil::core::Line line;
  line.points = {{100, 100}, {300, 100}};
  canvas.setLines({line});

  canvas.setScale(1.0);
  check(canvas.selectLineAt(200, 100 + t.lineRadiusPx - 0.5) == 0, "a stroke is hit inside lineRadiusPx");
  check(canvas.selectLineAt(200, 100 + t.lineRadiusPx + 0.5) == -1, "and missed past it");
  check(canvas.selectLineAt(100 - t.grabRadiusPx + 0.5, 100) == 0 && canvas.getSelectedPoint() == 0,
        "an end point is grabbed inside grabRadiusPx");
  check(canvas.selectLineAt(100 - t.grabRadiusPx - 0.5, 100) == -1, "and missed past it");
  canvas.setScale(2.0);
  check(canvas.selectLineAt(200, 100 + t.lineRadiusPx / 2 - 0.25) == 0, "zoomed in, the radius is screen px");
  check(canvas.selectLineAt(200, 100 + t.lineRadiusPx / 2 + 0.25) == -1, "so half as many image px");
  check(canvas.hitRadius(t.pointRadiusPx) == t.pointRadiusPx / 2, "the tooltip's point radius is screen px too");

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
