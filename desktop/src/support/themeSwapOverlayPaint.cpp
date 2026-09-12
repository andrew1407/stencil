#include "themeSwapOverlay.hpp"

namespace stencil::gui {

  // Snapshot `host` as it looks RIGHT NOW. Returns nullptr when there is nothing
  // worth animating (no host, not on screen yet, or a degenerate size) — callers
  // treat that as "just restyle", so a swap never depends on this succeeding.
  // `origin` is where the wipe starts, in host coordinates — the control that was
  // used, so the palette visibly comes out of the button you pressed (browser/
  // extension parity). A null/off-window origin falls back to the centre.
  ThemeSwapOverlay* ThemeSwapOverlay::capture(QWidget* host, QPoint origin) {
    if (!host || !host->isVisible() || host->width() < 2 || host->height() < 2) return nullptr;
    const QPixmap snap = host->grab();
    if (snap.isNull()) return nullptr;
    auto* fx = new ThemeSwapOverlay(host, snap);
    if (host->rect().contains(origin)) fx->origin_ = origin;
    return fx;
  }


  // Play the wipe. Call AFTER the restyle, so the new palette is what shows through.
  void ThemeSwapOverlay::start() {
    setGeometry(parentWidget()->rect());
    raise();
    show();
    const QPointF c = origin_.x() >= 0 ? QPointF(origin_) : QRectF(rect()).center();
    // Reach the furthest corner FROM THAT POINT — off-centre origins need a bigger
    // radius, or the far corner never gets repainted.
    full_ = std::hypot(std::max(c.x(), width() - c.x()),
                       std::max(c.y(), height() - c.y()));
    // The overlay outlives the snapshot by the wake: the last motes ignite near the
    // wipe's end and still get their whole life. Without dust nothing changes.
    const int total = kSwapMs + (dust_ ? kDustLifeMs : 0);
    // A plain CLOCK (milliseconds) on a timer at the screen's own refresh rate
    // (dustKit.hpp frameIntervalMs — not Qt's 60Hz animation timer); the wipe reads
    // its radius off swapEase so the AREA grows evenly, and the dust reads its own
    // life off the same clock — one timeline, two consumers.
    clock_.start();
    auto* tick = new QTimer(this);
    tick->setTimerType(Qt::PreciseTimer);
    tick->setInterval(support::frameIntervalMs(this));
    connect(tick, &QTimer::timeout, this, [this, tick, total] {
      timeMs_ = std::min(double(total), clock_.nsecsElapsed() / 1e6);
      // The WHOLE overlay is repainted each frame, not just the annulus the circle swept.
      // Repainting the ring alone is cheaper and looks identical — right up until a widget
      // UNDERNEATH repaints itself: the restyle that precedes the wipe queues repaints for
      // every control it touched, and each one lands on top of an overlay that is never
      // told to cover it again. That is how the colour chips showed their new dark border
      // while the snapshot around them was still light. Redrawing the full frame puts the
      // snapshot back over anything that surfaced, and it is only a clipped pixmap blit
      // (a PATH clip, rasterising a subtracted circle every frame, is the expensive one).
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
    // Everything EXCEPT the circle keeps the old snapshot; inside it the freshly
    // themed window shows through.
    //
    // The hole is a QRegion, not a subtracted QPainterPath. Path clipping is
    // rasterised per frame over the whole window, which made the wipe visibly stutter
    // on a large window; region clipping is a cheap span operation. The cost of that
    // is aliased circle edges, so antialiasing is off here — at this size and speed
    // the stepping is invisible, and a smooth 60fps is worth far more than a smooth
    // edge on a half-second transition.
    const QPoint c = origin_.x() >= 0 ? origin_ : rect().center();
    // Once the wipe itself is over only the wake is left — no snapshot to lay back.
    if (timeMs_ < kSwapMs) {
      // The hole is the RAGGED front (edgeRadiusAt), not a circle: a torn polygon
      // still clips as cheap spans — the expensive thing this replaced long ago was
      // an antialiased QPainterPath, and a QRegion from a polygon is not that.
      const double e = swapEase(std::min(1.0, timeMs_ / kSwapMs));
      QPolygon front;
      front.reserve(kEdgePoints);
      for (int k = 0; k < kEdgePoints; k++) {
        const double a = k * kTau / kEdgePoints;
        const double r = edgeRadiusAt(k, e, full_, style_);
        front << QPoint(qRound(c.x() + std::cos(a) * r), qRound(c.y() + std::sin(a) * r));
      }
      p.setClipRegion(QRegion(rect()).subtracted(QRegion(front)));
      p.drawPixmap(0, 0, snap_);
    }
    if (!dust_) return;
    // The wake is INSIDE the circle, over the freshly themed window — clip off. Round
    // grains, like every other cloud's — a couple of thousand alive at the peak, each
    // a ~5px disc blitted from the sprite cache (dustKit.hpp MoteSprites: a dozen
    // colours, a handful of sizes), so the wake costs a fraction of a frame.
    p.setClipping(false);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    DustMote mote;
    for (int i = 0; i < kDustMotes; i++) {
      if (!dustMoteAt(i, timeMs_, QPointF(c), full_, QSizeF(size()), &mote, style_)) continue;
      // Every grain is painted from the departing palette by its mix and its hash
      // (browser spawnSwapDust), in the style's own shape along its heading.
      const support::StyleFrame sf = support::styleFrame(style_, mote.life, mote.life, mote.w, mote.len, timeMs_);
      const double mix = style_ == support::ParticleStyle::Dust ? support::dustMix(mote.w, mote.accent) : sf.mix;
      QColor col = support::tintedStop(dustAccent_, dustShade_, mix, support::tintOf(mote.w), dustDark_);
      col.setAlphaF(std::clamp(mote.alpha * sf.glow, 0.0, 1.0));
      sprites_.draw(p, QPointF(mote.x + sf.sx, mote.y + sf.sy), mote.size / 2 * sf.scale, col,
                    support::grainShape(style_, mote.w), mote.heading);
    }
    p.setOpacity(1.0);
  }

  ThemeSwapOverlay::ThemeSwapOverlay(QWidget* host,
                                     const QPixmap& snap) : QWidget(host), snap_(snap) {
    setObjectName(kObjectName);   // findable without a Q_OBJECT (this class stays MOC-free)
    // Click-through: the window underneath is already live and fully interactive —
    // the wipe is decoration and must never eat half a second of input.
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    hide();
  }
}  // namespace stencil::gui
