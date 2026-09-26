#include "popupSlide.hpp"

#include <QEasingCurve>
#include <QEvent>
#include <QParallelAnimationGroup>
#include <QPointer>
#include <QPropertyAnimation>
#include <QTimer>
#include <QWidget>

namespace stencil::support {

  namespace {
    // One per popup, kept across its shows: a re-show stops the run still going.
    class PopupSlide : public QObject {
     public:
      explicit PopupSlide(QWidget* popup) : QObject(popup), popup(popup) {
        setObjectName(QStringLiteral("stencilPopupSlide"));
        popup->installEventFilter(this);
      }

      void start(const QPoint& origin) {
        settle(/*land=*/false);   // a re-show keeps the geometry it was just given
        popup->setWindowOpacity(0.0);
        ++run;
        // A tick later: Show can fire inside QMenu::popup(), and a list scrolls to its row at full size.
        QTimer::singleShot(0, this, [this, origin, at = run] {
          if (at != run || !popup) return;
          if (popup->isVisible()) play(origin);
          else popup->setWindowOpacity(1.0);
        });
      }

     protected:
      bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == popup && event->type() == QEvent::Hide) settle();
        return QObject::eventFilter(watched, event);
      }

     private:
      void play(const QPoint& origin) {
        target = popup->geometry();
        if (!target.isValid()) return popup->setWindowOpacity(1.0);
        savedMin = popup->minimumSize();
        popup->setMinimumSize(1, 1);   // setGeometry clamps to it; settle() restores it
        const QRect first = slideStartRect(target, origin);
        popup->setGeometry(first);
        QEasingCurve curve(QEasingCurve::BezierSpline);
        curve.addCubicBezierSegment(QPointF(0.16, 1.0), QPointF(0.3, 1.0), QPointF(1.0, 1.0));
        auto* geo = new QPropertyAnimation(popup, "geometry", this);
        geo->setDuration(POPUP_SLIDE_MS);
        geo->setStartValue(first);
        geo->setEndValue(target);
        geo->setEasingCurve(curve);
        auto* fade = new QPropertyAnimation(popup, "windowOpacity", this);
        fade->setDuration(POPUP_SLIDE_MS);
        fade->setKeyValueAt(0.0, 0.0);
        fade->setKeyValueAt(POPUP_SLIDE_FADE_AT, 1.0);
        fade->setKeyValueAt(1.0, 1.0);
        group = new QParallelAnimationGroup(this);
        group->addAnimation(geo);
        group->addAnimation(fade);
        connect(group, &QParallelAnimationGroup::finished, this, [this] { settle(); });
        playing = true;
        group->start(QAbstractAnimation::DeleteWhenStopped);
      }

      // Idempotent: lands on the exact target, or just unveils a list that never ran.
      void settle(bool land = true) {
        if (group) { group->stop(); group = nullptr; }
        if (!popup) return;
        popup->setWindowOpacity(1.0);
        if (!playing) return;
        playing = false;
        popup->setMinimumSize(savedMin);
        if (land && target.isValid() && popup->isVisible()) popup->setGeometry(target);
      }

      QPointer<QWidget> popup;
      QPointer<QParallelAnimationGroup> group;
      QRect target;
      QSize savedMin;
      int run = 0;   // which start() a pending tick belongs to
      bool playing = false;
    };
  }  // namespace

  void slidePopupIn(QWidget& popup, const QPoint& originGlobal) {
    auto* slide = popup.findChild<QObject*>(QStringLiteral("stencilPopupSlide"),
                                            Qt::FindDirectChildrenOnly);
    auto* runner = dynamic_cast<PopupSlide*>(slide);
    if (!runner) runner = new PopupSlide(&popup);
    runner->start(originGlobal);
  }

}  // namespace stencil::support
