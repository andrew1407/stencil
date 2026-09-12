#include "disintegrateOverlay.hpp"

namespace stencil::gui {

  void DisintegrateOverlay::retarget(const QPoint& delta) {
    if (delta.isNull()) return;
    picture_.translate(delta);
    target_ += QPointF(delta);
    update();
  }


  // Keep the cloud anchored to `w` for the rest of the flight: every tick re-maps the
  // control's top-left and shifts the cloud by however far it moved. A control revealed
  // beside a sibling is photographed where it sits at that instant, and the sibling's
  // slot then pushes it along the row. Browser twin: motion.js followDust (revealControls).
  // A tile flight IS its geometry; a surface flight shifts picture and target.
  void DisintegrateOverlay::setFollow(QWidget* w) {
    follow_ = w;
    followAt_ = w && parentWidget() ? w->mapTo(parentWidget(), QPoint(0, 0)) : QPoint();
  }


  // A child widget is clipped by its parent, so a flight that leaves the host (a dialog
  // dragged off it or taller than it, a menu past its edge) was cropped at the window
  // border. When it needs room the host hasn't got, the layer becomes a frameless,
  // input-transparent top-level window spanning the whole trip; `host` stays its
  // QObject parent, and picture_/target_ stay in HOST coords via `shift_`.
  void DisintegrateOverlay::placeForSurface(QWidget* host, const QRect& picture,
                                            const QPoint& target, bool escapeHost,
                                            bool alwaysEscape) {
    const QRect hostBox(host->mapToGlobal(QPoint(0, 0)), host->size());
    QRect need = surfaceLayerRect(picture.translated(hostBox.topLeft()),
                                  host->mapToGlobal(target));
    // Nothing to escape for: it fits (unless the caller wants the top layer even
    // then — see overSurface's alwaysEscape), the caller draws inside the host by
    // design, or there is no desktop to escape ONTO — offscreen's virtual screen is
    // a fixed box unrelated to any real one, so the tests keep the plain child layer.
    if (!escapeHost || (hostBox.contains(need) && !alwaysEscape) || !hostBox.isValid()
        || QGuiApplication::platformName() == QLatin1String("offscreen")) {
      setGeometry(host->rect());
      return;
    }
    // Never bigger than the desktop it can actually be seen on.
    QRect desktop;
    for (const QScreen* s : QGuiApplication::screens()) desktop |= s->geometry();
    if (desktop.isValid()) need &= desktop;
    if (need.width() < 8 || need.height() < 8) { setGeometry(host->rect()); return; }
    // Above the host, never focusable/clickable, no shadow. Qt::ToolTip, not Qt::Window:
    // a plain top-level window steals an open QMenu/QComboBox popup's platform grab and
    // closes it instantly (confirmed live) — ToolTip is the same non-activating kind
    // AppTooltip already floats over an open menu with.
    setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                   | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus
                   | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    shift_ = hostBox.topLeft() - need.topLeft();
    setGeometry(need);
  }


  // The snapshot, mixed towards `ink` — one pass over the picture, so every mote is
  // already lifted by the time it is drawn. An invalid ink leaves it exactly as taken.
  QPixmap DisintegrateOverlay::liftedToInk(const QPixmap& snap, const QColor& ink) {
    if (!ink.isValid() || snap.isNull()) return snap;
    QPixmap out = snap;
    QPainter p(&out);
    p.setCompositionMode(QPainter::CompositionMode_SourceAtop);   // tints, never spreads
    p.fillRect(out.rect(), QColor(ink.red(), ink.green(), ink.blue(),
                                  qRound(255 * kSurfaceInkMix)));
    return out;
  }

  DisintegrateOverlay::DisintegrateOverlay(QWidget* host,
                                           const QPixmap& snap) : QWidget(host), snap_(snap) {
    setObjectName(kObjectName);   // findable without a Q_OBJECT (this class stays MOC-free)
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    hide();
  }


  // The clock: a plain timer at the screen's refresh interval (dustKit.hpp
  // frameIntervalMs) reading a wall clock, not a QVariantAnimation — Qt's animation
  // timer ticks 60 times a second whatever the display does, and a cloud at 60 on a
  // 120Hz screen read as the coarser thing next to the browser's. The per-cell delays
  // own the shaping; the clock is linear.
  void DisintegrateOverlay::start(int ms) {
    ms_ = std::max(1, ms);
    clock_.start();
    auto* tick = new QTimer(this);
    tick->setTimerType(Qt::PreciseTimer);
    tick->setInterval(support::frameIntervalMs(this));
    connect(tick, &QTimer::timeout, this, [this, tick] {
      t_ = std::min(1.0, clock_.nsecsElapsed() / 1e6 / ms_);
      syncFollow();
      update();
      if (t_ >= 1.0) {
        tick->stop();
        deleteLater();
      }
    });
    tick->start();
  }


  // Dust motes sized on screen rather than as a share of the image, thinned back
  // if that would exceed the per-frame ceiling.
  void DisintegrateOverlay::sizeGridForDust(const QSize& size, int maxCells, int cellPx) {
    // Water and fire grid coarser (browser motion.js makeDustStage does the same).
    if (style_ != support::ParticleStyle::Dust) cellPx = qRound(cellPx * support::kStyledCellScale);
    dustGrid(size, cellPx, maxCells, &cols_, &rows_);
  }

  void DisintegrateOverlay::syncFollow() {
    if (!follow_ || !parentWidget()) return;
    const QPoint now = follow_->mapTo(parentWidget(), QPoint(0, 0));
    const QPoint delta = now - followAt_;
    if (delta.isNull()) return;
    followAt_ = now;
    if (picture_.isValid()) retarget(delta);
    else move(pos() + delta);
  }
}  // namespace stencil::gui
