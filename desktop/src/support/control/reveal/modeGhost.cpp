#include "modeGhost.hpp"

#include "../../motionPrefs.hpp"

#include <QEasingCurve>
#include <algorithm>
#include <QPainter>
#include <QVariantAnimation>

namespace stencil::gui {

  namespace {
    class ModeGhost : public QWidget {
     public:
      ModeGhost(QWidget* host, const QPixmap& shot, const QRect& at, const QRect& clip, bool in,
                Qt::Orientation axis, bool slide, bool pill)
          : QWidget(host), shot(shot), at(at.translated(-clip.topLeft())), in(in), axis(axis), slide(slide), pill(pill) {
        setObjectName(QStringLiteral("modeGhost"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setGeometry(clip);
        raise();
        show();
      }
      void setT(double v) { t = v; update(); }

     protected:
      void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const double away = in ? 1.0 - t : t;   // 0 = at rest, 1 = fully gone
        const double shown = 1.0 - away;
        if (slide) {
          const int dx = axis == Qt::Horizontal ? -static_cast<int>(away * (at.width() + (pill ? 12 : at.left()))) : 0;
          const int dy = axis == Qt::Vertical ? -static_cast<int>(away * (at.height() + (pill ? 12 : at.top()))) : 0;
          if (pill) p.setOpacity(in ? std::min(1.0, shown / 0.4) : std::max(0.0, 1.0 - away / 0.5));
          p.drawPixmap(at.translated(dx, dy), shot);
          return;
        }
        const double s = 1.0 - (pill ? 0.8 : 0.4) * away;   // up from 60% (browser revealGrow / rowGrowIn 78%), a chip 20%
        p.setOpacity(pill ? (in ? std::min(1.0, shown / 0.3) : std::max(0.0, 1.0 - away / 0.5)) : shown);
        QRectF r(0, 0, at.width() * s, at.height() * s);
        r.moveCenter(QRectF(at).center());
        p.drawPixmap(r, shot, QRectF(shot.rect()));
      }

     private:
      QPixmap shot;
      QRect at;
      bool in;
      Qt::Orientation axis;
      bool slide;
      bool pill;
      double t = 0.0;
    };
  }  // namespace

  bool modeGhostMotion() { return support::isSlideMotionOk() || support::isTransitionMotionOk(); }

  bool playModeGhost(QWidget* host, const QPixmap& shot, const QRect& at, const QRect& clip, bool in,
                     Qt::Orientation axis, int ms, std::function<void()> done, bool pill, bool slideAlways) {
    if (!modeGhostMotion() || !host || shot.isNull() || !at.isValid() || !clip.isValid()) return false;
    auto* ghost = new ModeGhost(host, shot, at, clip, in, axis, support::isSlideMotionOk() || slideAlways, pill);
    auto* anim = new QVariantAnimation(ghost);
    anim->setDuration(support::motionMs(ms));
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(in ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
    QObject::connect(anim, &QVariantAnimation::valueChanged, ghost, [ghost](const QVariant& v) { ghost->setT(v.toDouble()); });
    QObject::connect(anim, &QVariantAnimation::finished, ghost, [ghost, done = std::move(done)] {
      if (done) done();
      ghost->hide();   // gone as what it stood for shows, never one frame over it
      ghost->deleteLater();
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
    return true;
  }

}  // namespace stencil::gui
