#include "ThemeSwapOverlay.hpp"

namespace stencil::gui {

  // nullptr when there is nothing worth animating — callers then just restyle. `origin`
  // (host coords) is the control that was used; null/off-window falls back to the centre.
  ThemeSwapOverlay* ThemeSwapOverlay::capture(QWidget* host, QPoint origin) {
    if (!host || !host->isVisible() || host->width() < 2 || host->height() < 2) return nullptr;
    const QPixmap snap = host->grab();
    if (snap.isNull()) return nullptr;
    auto* fx = new ThemeSwapOverlay(host, snap);
    if (host->rect().contains(origin)) fx->origin_ = origin;
    return fx;
  }


  // Call AFTER the restyle, so the new palette is what shows through.
  void ThemeSwapOverlay::start() {
    setGeometry(parentWidget()->rect());
    raise();
    show();
    const QPointF c = origin_.x() >= 0 ? QPointF(origin_) : QRectF(rect()).center();
    // Off-centre origins need a bigger radius, or the far corner never gets repainted.
    full_ = std::hypot(std::max(c.x(), width() - c.x()),
                       std::max(c.y(), height() - c.y()));
    // The overlay outlives the snapshot by the wake so the last motes get their whole life.
    const int total = SWAP_MS + (dust_ ? DUST_LIFE_MS : 0);
    // A plain clock on a timer at the screen's refresh rate (dustKit.hpp frameIntervalMs,
    // not Qt's 60Hz animation timer); the wipe and the dust both read it.
    clock_.start();
    auto* tick = new QTimer(this);
    tick->setTimerType(Qt::PreciseTimer);
    tick->setInterval(support::frameIntervalMs(this));
    connect(tick, &QTimer::timeout, this, [this, tick, total] {
      timeMs_ = std::min(double(total), clock_.nsecsElapsed() / 1e6);
      // The WHOLE overlay repaints each frame: widgets UNDERNEATH repaint after the restyle
      // and would surface through a ring-only repaint. A clipped pixmap blit is cheap;
      // a PATH clip rasterising a subtracted circle per frame is not.
      update();
      if (timeMs_ >= total) {
        tick->stop();
        deleteLater();
      }
    });
    tick->start();
  }

  void ThemeSwapOverlay::paintEvent(QPaintEvent*) {
    if (snap_.isNull()) return;
    QPainter p(this);
    // The hole is a QRegion, not a QPainterPath: path clipping is rasterised per frame
    // over the whole window and stuttered; region clipping is spans. Antialiasing off.
    const QPoint c = origin_.x() >= 0 ? origin_ : rect().center();
    if (timeMs_ < SWAP_MS) {
      // A torn polygon still clips as cheap spans.
      const double e = swapEase(std::min(1.0, timeMs_ / SWAP_MS));
      QPolygon front;
      front.reserve(EDGE_POINTS);
      for (int k = 0; k < EDGE_POINTS; k++) {
        const double a = k * TAU / EDGE_POINTS;
        const double r = edgeRadiusAt(k, e, full_, style_);
        front << QPoint(qRound(c.x() + std::cos(a) * r), qRound(c.y() + std::sin(a) * r));
      }
      p.setClipRegion(QRegion(rect()).subtracted(QRegion(front)));
      p.drawPixmap(0, 0, snap_);
    }
    if (!dust_) return;
    // Clip inside the circle. Grains are ~5px discs blitted from the sprite cache (dustKit.hpp MoteSprites).
    p.setClipping(false);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    DustMote mote;
    for (int i = 0; i < DUST_MOTES; i++) {
      if (!dustMoteAt(i, timeMs_, QPointF(c), full_, QSizeF(size()), &mote, style_)) continue;
      // Painted from the departing palette by mix and hash (browser spawnSwapDust).
      const support::StyleFrame sf = support::styleFrame(style_, mote.life, mote.life, mote.w, mote.len, timeMs_);
      const double mix = style_ == support::ParticleStyle::DUST ? support::dustMix(mote.w, mote.accent) : sf.mix;
      QColor col = support::tintedStop(dustAccent_, dustShade_, mix, support::tintOf(mote.w), dustDark_);
      col.setAlphaF(std::clamp(mote.alpha * sf.glow, 0.0, 1.0));
      sprites_.draw(p, QPointF(mote.x + sf.sx, mote.y + sf.sy), mote.size / 2 * sf.scale, col,
                    support::grainShape(style_, mote.w), mote.heading);
    }
    p.setOpacity(1.0);
  }

  ThemeSwapOverlay::ThemeSwapOverlay(QWidget* host,
                                     const QPixmap& snap) : QWidget(host), snap_(snap) {
    setObjectName(OBJECT_NAME);   // findable without a Q_OBJECT (this class stays MOC-free)
    // Click-through: the wipe is decoration and must never eat input.
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    hide();
  }
}  // namespace stencil::gui
