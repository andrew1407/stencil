#include "disintegrateOverlay.hpp"

namespace stencil::gui {


  // Where a docked surface's dust comes from / returns to: `picture`'s centre pushed
  // out past the edge named by `area` by `reach`x that edge's own extent — the
  // browser's dockAwayPoint (motion.js).
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


  // The browser surfaceForm ramp: held at 0 until kDustHold, then up to 1 — the fade
  // every surface plays behind its own gathering dust. Works on any variant animation
  // (windowOpacity or a QGraphicsOpacityEffect's opacity alike).
  void holdFadeKeys(QVariantAnimation* fade, int ms) {
    fade->setKeyValues({});
    fade->setDuration(ms);
    fade->setKeyValueAt(0.0, 0.0);
    fade->setKeyValueAt(kDustHold, 0.0);
    fade->setKeyValueAt(1.0, 1.0);
  }


  // windowOpacity flavour for a top-level surface: waits invisible behind its own
  // gathering dust and fades up as the last motes land; never left dimmed.
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


  // Deterministic per-cell jitter — the same hash the browser uses, so the two
  // scatter alike. Returns 0..1.
  double DisintegrateOverlay::cellNoise(int cx, int cy) {
    const double h = std::sin(cx * 127.1 + cy * 311.7) * 43758.5453;
    return h - std::floor(h);
  }


  // The bend at `away` (0 home … 1 at the far end of the throw `tx, ty`): a push off
  // the throw's own line, by `q`'s side and amount. Shared with the combo's word
  // exchange (controlSwap.hpp), so the app's sand all bends alike.
  QPointF DisintegrateOverlay::swirlAt(double away, double tx, double ty, double q) {
    constexpr double kPi = 3.14159265358979323846;   // M_PI is not portable (MSVC)
    const double len = std::hypot(tx, ty);
    if (len < 0.5) return {};
    const double amp = (q - 0.5) * 2.0 * std::min(len * kSwirlShare, kSwirlMaxPx);
    const double s = std::sin(kPi * away) * amp;
    return QPointF(-ty / len * s, tx / len * s);
  }


  // The waypoint of a throw `tx, ty` (browser tileWaypoint): kWaypointAlong of the way
  // out, pushed perpendicular by `q`'s side and a capped share of the throw.
  QPointF DisintegrateOverlay::waypointOf(double tx, double ty, double q) {
    const double len = std::hypot(tx, ty);
    if (len < 0.5) return {};
    const double amp = (q - 0.5) * 2.0 * std::min(len * kSwirlShare, kSwirlMaxPx);
    return QPointF(tx * kWaypointAlong - ty / len * amp, ty * kWaypointAlong + tx / len * amp);
  }


  // A two-leg keyframe flight at time `t` (0..1): `a` to `b` over the first `split` on
  // curve `first`, then `b` to `c` on curve `second` — what CSS does with a mid
  // keyframe that carries its own animation-timing-function.
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


  // …and a scalar (the grain's size) on the same two legs.
  double DisintegrateOverlay::legScalar(double t, double split, const support::EaseLut& first,
                                        const support::EaseLut& second, double a, double b,
                                        double c) {
    if (t <= split) return a + (b - a) * first.at(split > 0 ? t / split : 1.0);
    return b + (c - b) * second.at((t - split) / (1.0 - split));
  }


  // The browser's curves, tabulated once (css/animations.css):
  // a surface's first leg into the bend, and its ease-out home (tileGatherSurface /
  // tileScatterSurface: cubic-bezier(0.3,0.3,0.6,0.8) then (0.16,1,0.3,1));
  const support::EaseLut& DisintegrateOverlay::surfaceLegEase() {
    static const support::EaseLut lut(0.3, 0.3, 0.6, 0.8);
    return lut;
  }

  const support::EaseLut& DisintegrateOverlay::surfaceEase() {
    static const support::EaseLut lut(0.16, 1.0, 0.3, 1.0);
    return lut;
  }


  // …and a row's (tileScatter: (0.3,0.4,0.7,0.8) into the bend at 38%, then the
  // flight's own (0.22,0.55,0.3,1) home).
  const support::EaseLut& DisintegrateOverlay::rowLegEase() {
    static const support::EaseLut lut(0.3, 0.4, 0.7, 0.8);
    return lut;
  }

  const support::EaseLut& DisintegrateOverlay::rowEase() {
    static const support::EaseLut lut(0.22, 0.55, 0.3, 1.0);
    return lut;
  }


  // The sideways push at progress `p` of a throw `tx, ty`, for a grain of hash `w`.
  QPointF DisintegrateOverlay::turbulenceAt(double p, double tx, double ty, double w) {
    constexpr double kPi = 3.14159265358979323846;
    const double len = std::hypot(tx, ty);
    if (len < 0.5) return {};
    const double waves = kTurbulenceWaves[0] + (kTurbulenceWaves[1] - kTurbulenceWaves[0]) * w;
    const double s = std::min(len * kTurbulenceShare, kTurbulenceMaxPx) * std::sin(kPi * p)
                   * std::sin(p * waves * 2 * kPi + w * 2 * kPi);
    return QPointF(-ty / len * s, tx / len * s);
  }


  // A glint's brightness at `ms` into the flight (1 for a plain grain).
  double DisintegrateOverlay::twinkleAt(bool glint, double ms, double w) {
    constexpr double kPi = 3.14159265358979323846;
    if (!glint) return 1.0;
    const double hz = kTwinkleHz[0] + (kTwinkleHz[1] - kTwinkleHz[0]) * w;
    return 1.0 - kTwinkleDepth * 0.5 * (1.0 + std::sin(ms * hz * 2 * kPi / 1000.0 + w * 2 * kPi));
  }


  // A grain's radius at home, for a `cw` x `ch` cell and its hash `n`.
  double DisintegrateOverlay::moteRadius(double cw, double ch, double n) {
    return std::min({cw, ch, double(kSpeckPx)}) * (0.62 + n * 0.5) * 0.5;
  }


  // A grain's alpha over its flight's TIME `k` (0..1) — the browser's tileScatter /
  // tileGather keyframes evaluated by hand. On the clock, not on the eased distance:
  // an ease-out covers most of the trip early, and a fade riding it was over before
  // the grain had visibly gone anywhere.
  double DisintegrateOverlay::scatterAlpha(double k) {
    return k < 0.38 ? 1.0 - k * (0.15 / 0.38) : 0.85 * (1.0 - (k - 0.38) / 0.62);
  }

  double DisintegrateOverlay::gatherAlpha(double k) {
    if (k < 0.22) return 0.75 * (k / 0.22);
    if (k < 0.58) return 0.75 + 0.15 * ((k - 0.22) / 0.36);
    return 0.9 + 0.1 * ((k - 0.58) / 0.42);
  }


  // The colour of every cell of `snap`, in one pass: the picture scaled down to the
  // grid — Qt's smooth scale is an area average — premultiplied so transparent pixels
  // weigh nothing. Read back with cellColour(), which lifts the coverage.
  QImage DisintegrateOverlay::sampleCells(const QPixmap& snap, int cols, int rows) {
    return snap.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied)
        .scaled(std::max(1, cols), std::max(1, rows), Qt::IgnoreAspectRatio,
                Qt::SmoothTransformation);
  }

  QColor DisintegrateOverlay::cellColour(const QImage& cells, int cx, int cy) {
    if (cells.isNull() || cx < 0 || cy < 0 || cx >= cells.width() || cy >= cells.height()) return {};
    QColor c = cells.pixelColor(cx, cy);
    c.setAlphaF(std::min(1.0, c.alphaF() * kCoverageLift));
    return c;
  }


  // Motes sized on SCREEN (`cellPx` each), thinned back if that would exceed the
  // ceiling — browser motion.js reshapeGrid. Shared with controlReveal's mark grid.
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
