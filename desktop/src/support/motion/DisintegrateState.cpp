#include "DisintegrateOverlay.hpp"

#include <QDockWidget>

namespace stencil::gui {

  void DisintegrateOverlay::retarget(const QPoint& delta) {
    if (delta.isNull()) return;
    picture.translate(delta);
    target += QPointF(delta);
    update();
  }


  // Browser twin: surface/motion.js followDust. A tile flight IS its geometry; a surface flight
  // shifts picture and target.
  void DisintegrateOverlay::setFollow(QWidget* w) {
    follow = w;
    followAt = w && parentWidget() ? w->mapTo(parentWidget(), QPoint(0, 0)) : QPoint();
  }


  QWidget* DisintegrateOverlay::surfaceOf(QWidget* inside) {
    QWidget* w = inside;
    while (w && !w->isWindow() && !qobject_cast<QDockWidget*>(w)) w = w->parentWidget();
    return w;
  }

  void DisintegrateOverlay::bindToSurface(QWidget* inside) {
    if (surface) surface->removeEventFilter(this);
    disconnect(surfaceGone);
    surface = surfaceOf(inside);
    if (!surface) return;
    surface->installEventFilter(this);
    surfaceGone = connect(surface, &QObject::destroyed, this, [this] { hide(); deleteLater(); });
  }

  // Hidden before the owner's own hide handlers run, so a close flight's photograph never holds it.
  bool DisintegrateOverlay::eventFilter(QObject* watched, QEvent* e) {
    if (watched == surface && (e->type() == QEvent::Hide || e->type() == QEvent::Close)) {
      hide();
      deleteLater();
    }
    return QWidget::eventFilter(watched, e);
  }


  // A child layer is clipped by the host, so a flight that needs more room becomes a frameless
  // input-transparent top-level; `host` stays the QObject parent, coords stay HOST via `shift`.
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
    shift = hostBox.topLeft() - need.topLeft();
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
                                           const QPixmap& snap) : QWidget(host), snap(snap) {
    setObjectName(OBJECT_NAME);   // findable without a Q_OBJECT (this class stays MOC-free)
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    hide();
  }


  // A plain timer at the screen's refresh interval, not a QVariantAnimation: Qt's
  // animation timer ticks 60Hz whatever the display does. The clock is linear.
  void DisintegrateOverlay::start(int ms) {
    this->ms = std::max(1, ms);
    clock.start();
    auto* tick = new QTimer(this);
    tick->setTimerType(Qt::PreciseTimer);
    tick->setInterval(support::frameIntervalMs(this));
    connect(tick, &QTimer::timeout, this, [this, tick] {
      t = std::min(1.0, clock.nsecsElapsed() / 1e6 / this->ms);
      syncFollow();
      update();
      if (t >= 1.0) {
        tick->stop();
        deleteLater();
      }
    });
    tick->start();
  }


  void DisintegrateOverlay::sizeGridForDust(const QSize& size, int maxCells, int cellPx) {
    // Water and fire grid coarser (browser surface/motion.js makeDustStage).
    if (style != support::ParticleStyle::DUST) cellPx = qRound(cellPx * support::STYLED_CELL_SCALE);
    dustGrid(size, cellPx, maxCells, &cols, &rows);
  }

  void DisintegrateOverlay::syncFollow() {
    if (!follow || !parentWidget()) return;
    const QPoint now = follow->mapTo(parentWidget(), QPoint(0, 0));
    const QPoint delta = now - followAt;
    if (delta.isNull()) return;
    followAt = now;
    if (picture.isValid()) retarget(delta);
    else move(pos() + delta);
  }
}  // namespace stencil::gui
