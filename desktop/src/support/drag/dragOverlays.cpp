#include "dragOverlays.hpp"
#include "iconDrag.hpp"
#include <QHash>
#include <QPainter>

namespace stencil::support {

  namespace {
    constexpr double GHOST_OPACITY = 0.85;   // the browser's GHOST_OPACITY (ui/drag/iconDrag.js)
    constexpr int GLOW_PAD = 6;              // px the glow stands off the target, room for its blur
    constexpr int GLOW_RADIUS = 7;

    // Keyed by target; an entry leaves with its target, taking the glow along.
    QHash<const QWidget*, QPointer<DropGlow>>& glows() {
      static QHash<const QWidget*, QPointer<DropGlow>> live;
      return live;
    }
  }  // namespace

  DragGhost::DragGhost(QWidget* source, const QPoint& grab, const QPixmap& picture)
      : QWidget(source->window()), face(picture.isNull() ? source->grab() : picture), grab(grab) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    resize((QSizeF(face.size()) / face.devicePixelRatio()).toSize());
    follow(source->mapToGlobal(grab));
    show();
  }

  void DragGhost::follow(const QPoint& global) {
    move(parentWidget()->mapFromGlobal(global) - grab);
    raise();
  }

  void DragGhost::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setOpacity(GHOST_OPACITY);
    p.drawPixmap(0, 0, face);
  }

  DropGlow::DropGlow(QWidget* target) : QWidget(target->window()), target(target) {
    setObjectName(QLatin1String(DROP_GLOW_NAME));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    track();
    show();
  }

  void DropGlow::setOver(bool over) {
    if (this->over == over) return;
    this->over = over;
    update();
  }

  void DropGlow::track() {
    if (!target) return;
    const QRect r(target->mapTo(parentWidget(), QPoint(0, 0)), target->size());
    setGeometry(r.adjusted(-GLOW_PAD, -GLOW_PAD, GLOW_PAD, GLOW_PAD));
    raise();
  }

  // Concentric rounded rims fading outward: the browser's 2px ring plus its blurred shadow.
  void DropGlow::paintEvent(QPaintEvent*) {
    if (!target) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    QColor accent = target->palette().color(QPalette::Highlight);
    const int rims = over ? GLOW_PAD : GLOW_PAD / 2;
    for (int i = rims; i >= 0; --i) {
      accent.setAlphaF((over ? 0.9 : 0.45) * (1.0 - double(i) / (rims + 1)));
      p.setPen(QPen(accent, i == 0 ? 2.0 : 1.0));
      const QRectF ring = QRectF(rect()).adjusted(GLOW_PAD - i, GLOW_PAD - i, i - GLOW_PAD, i - GLOW_PAD);
      p.drawRoundedRect(ring.adjusted(0.5, 0.5, -0.5, -0.5), GLOW_RADIUS + i, GLOW_RADIUS + i);
    }
  }

  void markDropTarget(QWidget* target, bool on, bool over) {
    if (!target) return;
    if (!on) {
      delete glows().take(target).data();
      return;
    }
    QPointer<DropGlow>& glow = glows()[target];
    if (!glow) {
      glow = new DropGlow(target);
      QObject::connect(target, &QObject::destroyed, glow, [target] { delete glows().take(target).data(); });
    }
    glow->setOver(over);
    glow->track();
  }

}  // namespace stencil::support
