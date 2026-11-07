// Headless check of the genie's band painting (src/support/genie/stripPaint.hpp): merged draws land every
// band within MERGE_TOL_PX, the transpose swaps x and y exactly, the column runs cover the dirty region, and
// a sideways flow painted transposed matches the same bands painted directly.
#include "../../../src/support/genie/stripPaint.hpp"

#include <QGuiApplication>
#include <cmath>

using namespace stencil::support::genie;

#include "../../support/check.hpp"

namespace {
  // The worst channel difference between two same-sized images.
  int worstDiff(const QImage& a, const QImage& b) {
    int worst = 0;
    for (int y = 0; y < a.height(); ++y)
      for (int x = 0; x < a.width(); ++x) {
        const QRgb p = a.pixel(x, y), q = b.pixel(x, y);
        worst = std::max({worst, std::abs(qRed(p) - qRed(q)), std::abs(qGreen(p) - qGreen(q)),
                          std::abs(qBlue(p) - qBlue(q)), std::abs(qAlpha(p) - qAlpha(q))});
      }
    return worst;
  }
  QImage canvas(QSize size) {
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    return img;
  }
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  const QRectF win(20, 10, 120, 160);

  // ── Merging: a window at rest is one draw; mid-flight every band still lands within the tolerance.
  for (Side side : {Side::DOWN, Side::UP, Side::RIGHT, Side::LEFT}) {
    const QRectF icon = side == Side::DOWN ? QRectF(70, 400, 24, 24) : side == Side::UP ? QRectF(70, -300, 24, 24)
                      : side == Side::RIGHT ? QRectF(500, 80, 24, 24) : QRectF(-400, 80, 24, 24);
    QVector<Strip> merged;
    mergeStrips(warp(win, icon, side, 0.0), merged);
    check(merged.size() == 1, "a window at rest is drawn in one piece");
    for (double p : {0.1, 0.3, 0.6, 0.9}) {
      const QVector<Strip> bands = warp(win, icon, side, p);
      mergeStrips(bands, merged);
      check(!merged.isEmpty() && merged.size() <= bands.size(), "merging never adds a draw");
      qsizetype k = 0;
      bool landed = true;
      for (const Strip& run : merged)
        for (; k < bands.size() && run.source.contains(bands[k].source.center()); ++k)
          landed = landed && within(mapped(run, bands[k].source), bands[k].target, MERGE_TOL_PX + 1e-9);
      check(k == bands.size() && landed, "every band lands within MERGE_TOL_PX of its own target");
    }
  }

  // ── The transpose: x and y swap, the pixel ratio kept.
  QImage src(5, 3, QImage::Format_ARGB32_Premultiplied);
  for (int y = 0; y < 3; ++y)
    for (int x = 0; x < 5; ++x) src.setPixel(x, y, qRgba(x * 40, y * 80, 7, 255));
  src.setDevicePixelRatio(2);
  const QImage t = transposed(src);
  bool swapped = t.size() == QSize(3, 5) && t.devicePixelRatio() == 2;
  for (int y = 0; swapped && y < 3; ++y)
    for (int x = 0; x < 5; ++x) swapped = swapped && t.pixel(y, x) == src.pixel(x, y);
  check(swapped, "transposed() swaps x and y and keeps the pixel ratio");

  // ── Column runs: the y-banded slivers of a column footprint regroup into one rect per x-run, covering all of it.
  QRegion dirty;
  dirty += QRect(0, 0, 10, 100);
  dirty += QRect(8, 20, 10, 30);
  dirty += QRect(40, 5, 6, 50);
  const QVector<QRect> cols = columnsOf(dirty);
  QRegion covered;
  for (const QRect& c : cols) covered += c;
  check(cols.size() == 2 && (dirty - covered).isEmpty(), "columnsOf covers the region in one rect per x-run");

  // ── A sideways flow painted transposed matches the same bands painted directly, at 1x and 2x.
  for (qreal dpr : {1.0, 2.0}) {
    QImage shot(QSize(120, 160) * dpr, QImage::Format_ARGB32_Premultiplied);
    shot.setDevicePixelRatio(dpr);
    for (int y = 0; y < shot.height(); ++y)
      for (int x = 0; x < shot.width(); ++x) shot.setPixel(x, y, qRgba((x * 3) & 255, (y * 5) & 255, (x ^ y) & 255, 255));
    for (double p : {0.15, 0.5}) {
      QVector<Strip> bands, merged, mergedT;
      bands = warp(win, QRectF(300, 60, 24, 24), Side::RIGHT, p);
      mergeStrips(bands, merged);
      transposeInto(merged, mergedT);
      const QSize size(360, 200);
      QImage direct = canvas(size * dpr), across = canvas(size * dpr);
      direct.setDevicePixelRatio(dpr);
      across.setDevicePixelRatio(dpr);
      {
        QPainter painter(&direct);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        drawStrips(painter, shot, merged, QPointF(0, 0), QRectF(QPointF(0, 0), size));
      }
      {
        QPainter painter(&across);
        drawStripsAcross(painter, transposed(shot), mergedT, QPointF(0, 0), QRegion(QRect(QPoint(0, 0), size)), dpr);
      }
      // Bilinear sampling rounds on a different axis once transposed: a few levels apart, never a misplaced band.
      check(worstDiff(direct, across) <= 24, "the transposed paint matches the direct one");
    }
  }
  return failures ? 1 : 0;
}
