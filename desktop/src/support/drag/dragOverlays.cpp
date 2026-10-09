#include "dragOverlays.hpp"
#include "iconDrag.hpp"
#include "motionPrefs.hpp"

#include <QHash>
#include <QPainter>
#include <cmath>

namespace stencil::support {

  namespace {
    constexpr double GHOST_OPACITY = 0.85;   // the browser's GHOST_OPACITY (ui/drag/iconDrag.js)
    constexpr int GLOW_PAD = 6;              // px the glow stands off the target, room for its blur
    constexpr int GLOW_RADIUS = 7;
    constexpr int SHINE_BEAT_MS = 1400;      // one breath of the ghost's rim (css icon/drag.css)
    constexpr int FRAME_MS = 16;

    // Concentric rounded rims fading outward from `box`, `rims` deep: the browser's ring and blur.
    void paintRims(QPainter& p, const QRectF& box, QColor colour, int rims, double alpha, double width) {
      for (int i = rims; i >= 0; --i) {
        colour.setAlphaF(alpha * (1.0 - double(i) / (rims + 1)));
        p.setPen(QPen(colour, i == 0 ? width : 1.0));
        const QRectF ring = box.adjusted(-i, -i, i, i);
        p.drawRoundedRect(ring.adjusted(0.5, 0.5, -0.5, -0.5), GLOW_RADIUS + i, GLOW_RADIUS + i);
      }
    }

    // Keyed by target; an entry leaves with its target, taking the glow along.
    QHash<const QWidget*, QPointer<DropGlow>>& glows() {
      static QHash<const QWidget*, QPointer<DropGlow>> live;
      return live;
    }
  }  // namespace

  double ghostShine(double ms, bool still) {
    return still ? 1.0 : 0.5 - 0.5 * std::cos(2 * M_PI * ms / SHINE_BEAT_MS);
  }

  // The widget stands GLOW_PAD off the face on every side, room for the rim's shine.
  DragGhost::DragGhost(QWidget* source, const QPoint& grab, const QPixmap& picture)
      : QWidget(source->window()), face(picture.isNull() ? source->grab() : picture),
        grab(grab + QPoint(GLOW_PAD, GLOW_PAD)), accent(source->palette().color(QPalette::Highlight)) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    resize((QSizeF(face.size()) / face.devicePixelRatio()).toSize() + QSize(2 * GLOW_PAD, 2 * GLOW_PAD));
    follow(source->mapToGlobal(grab));
    clock.start();
    if (!motionReduced()) {
      connect(&ticker, &QTimer::timeout, this, qOverload<>(&QWidget::update));
      ticker.start(FRAME_MS);
    }
    show();
  }

  QSize DragGhost::faceSize() const { return size() - QSize(2 * GLOW_PAD, 2 * GLOW_PAD); }

  void DragGhost::follow(const QPoint& global) {
    move(parentWidget()->mapFromGlobal(global) - grab);
    raise();
  }

  void DragGhost::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setOpacity(GHOST_OPACITY);
    p.drawPixmap(GLOW_PAD, GLOW_PAD, face);
    p.setOpacity(1.0);
    const double beat = ghostShine(clock.elapsed(), motionReduced());
    const QRectF box = QRectF(rect()).adjusted(GLOW_PAD, GLOW_PAD, -GLOW_PAD, -GLOW_PAD);
    paintRims(p, box, accent, GLOW_PAD, 0.4 + 0.5 * beat, 1.5);
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
    const QRectF box = QRectF(rect()).adjusted(GLOW_PAD, GLOW_PAD, -GLOW_PAD, -GLOW_PAD);
    paintRims(p, box, target->palette().color(QPalette::Highlight), over ? GLOW_PAD : GLOW_PAD / 2,
              over ? 0.9 : 0.45, 2.0);
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
