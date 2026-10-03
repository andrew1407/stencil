// The canvas's highlight and divider metrics come from constants.json (FOCUS_RING, HOVER_RING,
// SELECT_GLOW, COMPARE_DIVIDER): the focused point wears the browser's ring and glow, the rest
// keep the values the desktop always drew, and the divider takes a press within its slack.
#include "CanvasWidget.hpp"
#include "markMetrics.hpp"

#include <QApplication>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>

#include "../../support/check.hpp"

using stencil::gui::CanvasWidget;
namespace metrics = stencil::gui::markMetrics;

namespace {
  double tableValue(const char* section, const char* key) {
    QFile f(QStringLiteral(":/config/constants.json"));
    if (!f.open(QIODevice::ReadOnly)) return -1;
    return QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String(section)).toObject()
        .value(QLatin1String(key)).toDouble(-1);
  }

  // One key: the table's value, the one the canvas reads, the no-qrc fallback and the expected value agree.
  void pin(const char* section, const char* key, double read, double fallback, double expected) {
    const QByteArray what = QByteArray(section) + "." + key;
    check(tableValue(section, key) == read, (what + " is read from constants.json").constData());
    check(fallback == read, (what + " falls back to the table's value").constData());
    check(read == expected, (what + " draws the expected value").constData());
  }

  bool moveShowsSplitCursor(CanvasWidget& canvas, int x, int y) {
    canvas.unsetCursor();
    QMouseEvent move(QEvent::MouseMove, QPointF(x, y), canvas.mapToGlobal(QPointF(x, y)), Qt::NoButton,
                     Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&canvas, &move);
    return canvas.cursor().shape() == Qt::SplitHCursor;
  }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  const metrics::Table& t = metrics::table();
  const metrics::Table f;

  // The focused ring is the browser's (the desktop drew +3 / 2 before it read the table).
  pin("FOCUS_RING", "gapPx", t.focusGapPx, f.focusGapPx, 6);
  pin("FOCUS_RING", "widthPx", t.focusWidthPx, f.focusWidthPx, 3);
  pin("FOCUS_RING", "blurPx", t.focusBlurPx, f.focusBlurPx, 12);
  pin("FOCUS_RING", "glowAlpha", t.focusGlowAlpha, f.focusGlowAlpha, 0.9);
  pin("HOVER_RING", "gapPx", t.hoverGapPx, f.hoverGapPx, 4);
  pin("HOVER_RING", "widthPx", t.hoverWidthPx, f.hoverWidthPx, 1.8);
  pin("HOVER_RING", "alpha", t.hoverAlpha, f.hoverAlpha, 0.55);
  pin("SELECT_GLOW", "pointGapPx", t.pointGlowGapPx, f.pointGlowGapPx, 4);
  pin("SELECT_GLOW", "pointAlpha", t.pointGlowAlpha, f.pointGlowAlpha, 0.5);
  pin("SELECT_GLOW", "linePadPx", t.lineGlowPadPx, f.lineGlowPadPx, 8);
  pin("SELECT_GLOW", "lineAlpha", t.lineGlowAlpha, f.lineGlowAlpha, 0.6);
  pin("SELECT_GLOW", "lineHoverPadPx", t.lineHoverPadPx, f.lineHoverPadPx, 6);
  pin("SELECT_GLOW", "lineHoverAlpha", t.lineHoverAlpha, f.lineHoverAlpha, 0.35);
  pin("COMPARE_DIVIDER", "lineWidthPx", t.dividerLineWidthPx, f.dividerLineWidthPx, 2);
  pin("COMPARE_DIVIDER", "knobRadiusPx", t.dividerKnobRadiusPx, f.dividerKnobRadiusPx, 7);
  pin("COMPARE_DIVIDER", "grabSlackPx", t.dividerGrabSlackPx, f.dividerGrabSlackPx, 8);
  pin("MARKER_RING", "darkFromLuma", t.ringDarkFromLuma, f.ringDarkFromLuma, 128);
  check(metrics::ringFor(QColor(255, 255, 0)) == QColor(0, 0, 0), "a yellow point's ring is black");
  check(metrics::ringFor(QColor(255, 0, 0)) == QColor(255, 255, 255), "a red point's ring is white");
  check(metrics::ringFor(QColor(128, 128, 128)) == QColor(0, 0, 0)
        && metrics::ringFor(QColor(127, 127, 127)) == QColor(255, 255, 255), "split at luma 128, as the core");

  CanvasWidget canvas;
  QImage page(200, 200, QImage::Format_RGB32);
  page.fill(Qt::white);
  canvas.loadFromImage(page, stencil::core::CropRect{0, 0, 200, 200}, 0);
  canvas.setHighlightColors(QColor("#ffc800"), QColor("#2563eb"), QColor("#ff0000"));
  stencil::core::Line line;
  line.points = {{60, 100}, {140, 100}};
  line.pointSize = 4;
  line.thickness = 2;
  line.color = "#000000";
  canvas.setLines({line});
  canvas.setScale(1.0);
  check(canvas.selectLineAt(60, 100) == 0 && canvas.getSelectedPoint() == 0, "the first point is focused");

  QImage shot(canvas.size(), QImage::Format_ARGB32_Premultiplied);
  shot.fill(Qt::white);
  canvas.render(&shot);
  const double ring = line.pointSize + t.focusGapPx;
  const QColor onRing = shot.pixelColor(60, qRound(100 - ring));
  check(onRing.red() > 220 && onRing.green() < 90 && onRing.blue() < 90, "the focus ring sits at r + gapPx");
  const QColor disc = shot.pixelColor(60, qRound(100 - line.pointSize - 2));
  check(disc.red() > 240 && disc.green() > 150 && disc.blue() < disc.green() - 40, "the selection disc fills inside it");
  const QColor glow = shot.pixelColor(60, qRound(100 - ring - t.focusWidthPx / 2 - 2));
  check(glow.red() > 240 && glow.green() < 250 && glow.green() > 120, "a soft glow spreads past the ring");
  const QColor far = shot.pixelColor(60, qRound(100 - ring - t.focusBlurPx * 2));
  check(far == QColor(Qt::white), "and fades out within two blurs");

  canvas.setCompareMode(QStringLiteral("vertical"));
  const int divider = qRound(canvas.getCompareSplit() * 200);
  check(moveShowsSplitCursor(canvas, divider + int(t.dividerGrabSlackPx), 40),
        "the divider takes the pointer within grabSlackPx");
  check(!moveShowsSplitCursor(canvas, divider + int(t.dividerGrabSlackPx) + 2, 40), "and not past it");

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
