// The in-window fullscreen glide (browser ui/motion/flip.js glideFrom): over the window as it now stands,
// the view's box grows or shrinks from where it stood to where it stands, its content centred and unscaled.
// Mouse-through, on the flip clock (motion.json FLIP_MS / FLIP_EASING), gone as it lands.
#pragma once

#include "../../support/motionPrefs.hpp"
#include "../../support/uiTimings.hpp"

#include <QEvent>
#include <QPainter>
#include <QPointer>
#include <QVariantAnimation>
#include <QWidget>

#include <functional>

namespace stencil::gui {

  class BoxGlide : public QWidget {
   public:
    // `view` (a child of `host`) stands where it lands; `from` (host-local) is where it stood. `live`, when
    // set and true, re-photographs the view each frame: something inside it is still moving (a zoom ride).
    static void play(QWidget* host, QWidget* view, const QRect& from, std::function<bool()> live = {}) {
      if (!host || !view || support::motionReduced() || !host->isVisible() || !from.isValid()) return;
      for (QWidget* old : host->findChildren<QWidget*>(QStringLiteral("boxGlide"), Qt::FindDirectChildrenOnly))
        delete old;
      const QRect to(view->mapTo(host, QPoint(0, 0)), view->size());
      if (!to.isValid() || to == from) return;
      new BoxGlide(host, view, from, to, std::move(live));
    }

   protected:
    bool eventFilter(QObject* o, QEvent* e) override {
      if (o == parentWidget() && e->type() == QEvent::Resize) deleteLater();   // the picture no longer fits
      return false;
    }
    void paintEvent(QPaintEvent*) override {
      QPainter p(this);
      p.drawPixmap(0, 0, base);
      const QRectF r(lerp(from.left(), to.left()), lerp(from.top(), to.top()),
                     lerp(from.width(), to.width()), lerp(from.height(), to.height()));
      p.fillRect(r, ground);
      p.setClipRect(r);
      p.drawPixmap(r.center() - QPointF(to.width(), to.height()) / 2.0, content);
    }

   private:
    BoxGlide(QWidget* host, QWidget* view, const QRect& from, const QRect& to, std::function<bool()> live)
        : QWidget(host), view(view), from(from), to(to), live(std::move(live)) {
      setObjectName(QStringLiteral("boxGlide"));
      setAttribute(Qt::WA_TransparentForMouseEvents);
      base = host->grab();
      content = view->grab();
      // The view's own ground, inside its hairline, where no picture or card sits.
      const QImage px = content.copy(QRect(QPoint(4, 4) * content.devicePixelRatio(), QSize(1, 1))).toImage();
      ground = px.isNull() ? host->palette().color(QPalette::Window) : px.pixelColor(0, 0);
      QPainter(&base).fillRect(to, ground);
      setGeometry(host->rect());
      host->installEventFilter(this);
      raise();
      show();
      auto* anim = new QVariantAnimation(this);
      anim->setDuration(support::motionMs(support::flipMotion().ms));
      anim->setEasingCurve(support::flipMotion().easing);
      anim->setStartValue(0.0);
      anim->setEndValue(1.0);
      connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        t = v.toDouble();
        if (this->live && this->live() && this->view) content = this->view->grab();
        update();
      });
      connect(anim, &QVariantAnimation::finished, this, &QObject::deleteLater);
      anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
    qreal lerp(qreal a, qreal b) const { return a + (b - a) * t; }

    QPointer<QWidget> view;
    QRect from, to;
    std::function<bool()> live;
    QPixmap base, content;
    QColor ground;
    qreal t = 0;
  };

}  // namespace stencil::gui
