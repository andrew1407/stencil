#include "disintegrateOverlay.hpp"

namespace stencil::gui {

  void DisintegrateOverlay::retarget(const QPoint& delta) {
    if (delta.isNull()) return;
    picture_.translate(delta);
    target_ += QPointF(delta);
    update();
  }


  // Browser twin: motion.js followDust. A tile flight IS its geometry; a surface flight
  // shifts picture and target.
  void DisintegrateOverlay::setFollow(QWidget* w) {
    follow_ = w;
    followAt_ = w && parentWidget() ? w->mapTo(parentWidget(), QPoint(0, 0)) : QPoint();
  }


  // A child layer is clipped by the host, so a flight that needs more room becomes a
  // frameless input-transparent top-level; `host` stays the QObject parent and
  // picture_/target_ stay in HOST coords via `shift_`.
  void DisintegrateOverlay::placeForSurface(QWidget* host, const QRect& picture,
                                            const QPoint& target, bool escapeHost,
                                            bool alwaysEscape) {
    const QRect hostBox(host->mapToGlobal(QPoint(0, 0)), host->size());
    QRect need = surfaceLayerRect(picture.translated(hostBox.topLeft()),
                                  host->mapToGlobal(target));
    // Offscreen's virtual screen is a fixed box unrelated to any real one: tests keep the child layer.
    if (!escapeHost || (hostBox.contains(need) && !alwaysEscape) || !hostBox.isValid()
        || QGuiApplication::platformName() == QLatin1String("offscreen")) {
      setGeometry(host->rect());
      return;
    }
    QRect desktop;
    for (const QScreen* s : QGuiApplication::screens()) desktop |= s->geometry();
    if (desktop.isValid()) need &= desktop;
    if (need.width() < 8 || need.height() < 8) { setGeometry(host->rect()); return; }
    // Qt::ToolTip, not Qt::Window: a plain top-level steals an open QMenu/QComboBox
    // popup's platform grab and closes it instantly (confirmed live).
    setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                   | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus
                   | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    shift_ = hostBox.topLeft() - need.topLeft();
    setGeometry(need);
  }


  // One pass, so every mote is already lifted when drawn. Invalid ink = as taken.
  QPixmap DisintegrateOverlay::liftedToInk(const QPixmap& snap, const QColor& ink) {
    if (!ink.isValid() || snap.isNull()) return snap;
    QPixmap out = snap;
    QPainter p(&out);
    p.setCompositionMode(QPainter::CompositionMode_SourceAtop);   // tints, never spreads
    p.fillRect(out.rect(), QColor(ink.red(), ink.green(), ink.blue(),
                                  qRound(255 * SURFACE_INK_MIX)));
    return out;
  }

  DisintegrateOverlay::DisintegrateOverlay(QWidget* host,
                                           const QPixmap& snap) : QWidget(host), snap_(snap) {
    setObjectName(OBJECT_NAME);   // findable without a Q_OBJECT (this class stays MOC-free)
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    hide();
  }


  // A plain timer at the screen's refresh interval, not a QVariantAnimation: Qt's
  // animation timer ticks 60Hz whatever the display does. The clock is linear.
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


  void DisintegrateOverlay::sizeGridForDust(const QSize& size, int maxCells, int cellPx) {
    // Water and fire grid coarser (browser motion.js makeDustStage).
    if (style_ != support::ParticleStyle::Dust) cellPx = qRound(cellPx * support::STYLED_CELL_SCALE);
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
