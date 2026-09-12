#include "DisintegrateOverlay.hpp"

namespace stencil::gui {


  // Browser motion.js dockAwayPoint: the centre pushed `reach`x the edge's extent past it.
  QPoint dockAwayPoint(const QRect& picture, Qt::DockWidgetArea area, double reach) {
    const bool horiz = area != Qt::TopDockWidgetArea && area != Qt::BottomDockWidgetArea;
    const int reachPx = qRound((horiz ? picture.width() : picture.height()) * reach);
    QPoint target = picture.center();
    if (area == Qt::LeftDockWidgetArea) target.setX(picture.left() - reachPx);
    else if (area == Qt::RightDockWidgetArea) target.setX(picture.right() + reachPx);
    else if (area == Qt::TopDockWidgetArea) target.setY(picture.top() - reachPx);
    else if (area == Qt::BottomDockWidgetArea) target.setY(picture.bottom() + reachPx);
    return target;
  }


  // Browser surfaceForm ramp: 0 until DUST_HOLD, then up to 1.
  void holdFadeKeys(QVariantAnimation* fade, int ms) {
    fade->setKeyValues({});
    fade->setDuration(ms);
    fade->setKeyValueAt(0.0, 0.0);
    fade->setKeyValueAt(DUST_HOLD, 0.0);
    fade->setKeyValueAt(1.0, 1.0);
  }


  void fadeUpBehindDust(QWidget* w, int ms) {
    auto* fade = new QPropertyAnimation(w, "windowOpacity", w);
    holdFadeKeys(fade, ms);
    QPointer<QWidget> guard(w);
    QObject::connect(fade, &QPropertyAnimation::finished, w, [guard] {
      if (guard) guard->setWindowOpacity(1.0);
    });
    w->setWindowOpacity(0.0);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
  }


  // The browser's hash, so the two scatter alike. 0..1.
  double DisintegrateOverlay::cellNoise(int cx, int cy) {
    const double h = std::sin(cx * 127.1 + cy * 311.7) * 43758.5453;
    return h - std::floor(h);
  }


  QPointF DisintegrateOverlay::swirlAt(double away, double tx, double ty, double q) {
    constexpr double PI = 3.14159265358979323846;   // M_PI is not portable (MSVC)
    const double len = std::hypot(tx, ty);
    if (len < 0.5) return {};
    const double amp = (q - 0.5) * 2.0 * std::min(len * SWIRL_SHARE, SWIRL_MAX_PX);
    const double s = std::sin(PI * away) * amp;
    return QPointF(-ty / len * s, tx / len * s);
  }


  // Browser tileWaypoint.
  QPointF DisintegrateOverlay::waypointOf(double tx, double ty, double q) {
    const double len = std::hypot(tx, ty);
    if (len < 0.5) return {};
    const double amp = (q - 0.5) * 2.0 * std::min(len * SWIRL_SHARE, SWIRL_MAX_PX);
    return QPointF(tx * WAYPOINT_ALONG - ty / len * amp, ty * WAYPOINT_ALONG + tx / len * amp);
  }


  // A CSS mid keyframe with its own timing function: a→b over `split`, then b→c.
  QPointF DisintegrateOverlay::legAt(double t, double split, const support::EaseLut& first,
                                     const support::EaseLut& second, const QPointF& a,
                                     const QPointF& b, const QPointF& c) {
    if (t <= split) {
      const double e = first.at(split > 0 ? t / split : 1.0);
      return a + (b - a) * e;
    }
    const double e = second.at((t - split) / (1.0 - split));
    return b + (c - b) * e;
  }


  double DisintegrateOverlay::legScalar(double t, double split, const support::EaseLut& first,
                                        const support::EaseLut& second, double a, double b,
                                        double c) {
    if (t <= split) return a + (b - a) * first.at(split > 0 ? t / split : 1.0);
    return b + (c - b) * second.at((t - split) / (1.0 - split));
  }


  // The browser's curves (css/animations.css tileGatherSurface / tileScatterSurface).
  const support::EaseLut& DisintegrateOverlay::surfaceLegEase() {
    static const support::EaseLut lut(0.3, 0.3, 0.6, 0.8);
    return lut;
  }

  const support::EaseLut& DisintegrateOverlay::surfaceEase() {
    static const support::EaseLut lut(0.16, 1.0, 0.3, 1.0);
    return lut;
  }


  // tileScatter's.
  const support::EaseLut& DisintegrateOverlay::rowLegEase() {
    static const support::EaseLut lut(0.3, 0.4, 0.7, 0.8);
    return lut;
  }

  const support::EaseLut& DisintegrateOverlay::rowEase() {
    static const support::EaseLut lut(0.22, 0.55, 0.3, 1.0);
    return lut;
  }


  QPointF DisintegrateOverlay::turbulenceAt(double p, double tx, double ty, double w) {
    constexpr double PI = 3.14159265358979323846;
    const double len = std::hypot(tx, ty);
    if (len < 0.5) return {};
    const double waves = TURBULENCE_WAVES[0] + (TURBULENCE_WAVES[1] - TURBULENCE_WAVES[0]) * w;
    const double s = std::min(len * TURBULENCE_SHARE, TURBULENCE_MAX_PX) * std::sin(PI * p)
                   * std::sin(p * waves * 2 * PI + w * 2 * PI);
    return QPointF(-ty / len * s, tx / len * s);
  }


  double DisintegrateOverlay::twinkleAt(bool glint, double ms, double w) {
    constexpr double PI = 3.14159265358979323846;
    if (!glint) return 1.0;
    const double hz = TWINKLE_HZ[0] + (TWINKLE_HZ[1] - TWINKLE_HZ[0]) * w;
    return 1.0 - TWINKLE_DEPTH * 0.5 * (1.0 + std::sin(ms * hz * 2 * PI / 1000.0 + w * 2 * PI));
  }


  double DisintegrateOverlay::moteRadius(double cw, double ch, double n) {
    return std::min({cw, ch, double(SPECK_PX)}) * (0.62 + n * 0.5) * 0.5;
  }


  // Browser tileScatter / tileGather alpha stops, on the CLOCK: a fade riding the eased
  // distance was over before the grain had visibly moved.
  double DisintegrateOverlay::scatterAlpha(double k) {
    return k < 0.38 ? 1.0 - k * (0.15 / 0.38) : 0.85 * (1.0 - (k - 0.38) / 0.62);
  }

  double DisintegrateOverlay::gatherAlpha(double k) {
    if (k < 0.22) return 0.75 * (k / 0.22);
    if (k < 0.58) return 0.75 + 0.15 * ((k - 0.22) / 0.36);
    return 0.9 + 0.1 * ((k - 0.58) / 0.42);
  }


  // Qt's smooth scale is an area average; premultiplied so transparent pixels weigh nothing.
  QImage DisintegrateOverlay::sampleCells(const QPixmap& snap, int cols, int rows) {
    return snap.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied)
        .scaled(std::max(1, cols), std::max(1, rows), Qt::IgnoreAspectRatio,
                Qt::SmoothTransformation);
  }

  QColor DisintegrateOverlay::cellColour(const QImage& cells, int cx, int cy) {
    if (cells.isNull() || cx < 0 || cy < 0 || cx >= cells.width() || cy >= cells.height()) return {};
    QColor c = cells.pixelColor(cx, cy);
    c.setAlphaF(std::min(1.0, c.alphaF() * COVERAGE_LIFT));
    return c;
  }


  // Browser motion.js reshapeGrid.
  void DisintegrateOverlay::dustGrid(const QSize& size, int cellPx, int maxCells, int* cols,
                                     int* rows) {
    *cols = std::max(1, qRound(double(size.width()) / cellPx));
    *rows = std::max(1, qRound(double(size.height()) / cellPx));
    while (*cols * *rows > std::max(64, maxCells)) {
      // ceil(x/1.1) is x itself for x <= 10 — force a strict shrink or this spins forever.
      *cols = std::max(1, std::min(*cols - 1, int(std::ceil(*cols / 1.1))));
      *rows = std::max(1, std::min(*rows - 1, int(std::ceil(*rows / 1.1))));
    }
  }
}  // namespace stencil::gui
