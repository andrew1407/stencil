#include "menuReveal.hpp"
#include "modalReveal.hpp"  // motionReduced()

#include <QAbstractAnimation>
#include <QEasingCurve>
#include <QEvent>
#include <QGuiApplication>
#include <QMenu>
#include <QParallelAnimationGroup>
#include <QPointer>
#include <QPropertyAnimation>
#include <QRect>

namespace stencil::support {

  namespace {
    // Snappier than the dialog flight — a menu should feel instant, not staged.
    constexpr int kMenuMs = 140;

    // One-shot filter, parented to the menu: plays the growth on the first Show,
    // and settles (restores constraints/opacity/geometry) on finish or on an
    // early Hide (Escape mid-flight).
    class MenuReveal : public QObject {
     public:
      MenuReveal(QMenu* menu, const QPoint& origin)
          : QObject(menu), menu_(menu), origin_(origin) {
        menu->installEventFilter(this);
      }

     protected:
      bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == menu_ && event->type() == QEvent::Show && !played_) {
          played_ = true;
          play();
        } else if (watched == menu_ && event->type() == QEvent::Hide) {
          settle();
        }
        return QObject::eventFilter(watched, event);
      }

     private:
      void play() {
        QMenu* m = menu_;
        // exec()/popup() has already placed the popup by Show time.
        target_ = m->geometry();
        if (!target_.isValid()) return;
        // Start box: ~1/3 size, keeping the click point at the same fractional
        // spot it has in the final rect, so the growth radiates from the cursor.
        const QPoint a(qBound(target_.left(), origin_.x(), target_.right()),
                       qBound(target_.top(), origin_.y(), target_.bottom()));
        const int w = qMax(target_.width() / 3, 24);
        const int h = qMax(target_.height() / 3, 24);
        const double fx = double(a.x() - target_.left()) / qMax(target_.width(), 1);
        const double fy = double(a.y() - target_.top()) / qMax(target_.height(), 1);
        const QRect start(QPoint(a.x() - int(fx * w), a.y() - int(fy * h)), QSize(w, h));
        // Lift the min-size for the flight (the chat menu is fixed-width, and
        // setGeometry clamps to constraints); settle() puts it back.
        savedMin_ = m->minimumSize();
        m->setMinimumSize(1, 1);
        m->setWindowOpacity(0.0);
        m->setGeometry(start);

        auto* geo = new QPropertyAnimation(m, "geometry", this);
        geo->setDuration(kMenuMs);
        geo->setStartValue(start);
        geo->setEndValue(target_);
        geo->setEasingCurve(QEasingCurve::OutCubic);
        auto* fade = new QPropertyAnimation(m, "windowOpacity", this);
        fade->setDuration(kMenuMs);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        fade->setEasingCurve(QEasingCurve::OutCubic);
        group_ = new QParallelAnimationGroup(this);
        group_->addAnimation(geo);
        group_->addAnimation(fade);
        connect(group_, &QParallelAnimationGroup::finished, this, [this] { settle(); });
        group_->start(QAbstractAnimation::DeleteWhenStopped);
      }

      // Land at the real box and undo everything play() overrode. Idempotent.
      void settle() {
        if (group_) { group_->stop(); group_ = nullptr; }  // stop → DeleteWhenStopped
        if (!menu_ || !played_ || settled_) return;
        settled_ = true;
        menu_->setMinimumSize(savedMin_);
        menu_->setWindowOpacity(1.0);
        if (target_.isValid() && menu_->isVisible()) menu_->setGeometry(target_);
      }

      QPointer<QMenu> menu_;
      QPointer<QParallelAnimationGroup> group_;
      QPoint origin_;
      QRect target_;
      QSize savedMin_;
      bool played_ = false;
      bool settled_ = false;
    };
  }  // namespace

  void revealMenu(QMenu& menu, const QPoint& origin) {
    // Offscreen has no compositor for windowOpacity, and the gui tests pick
    // items the instant the popup lands — both want the plain pop.
    if (motionReduced()) return;
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) return;
    new MenuReveal(&menu, origin);  // owned by the menu
  }

}  // namespace stencil::support
