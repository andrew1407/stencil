#include "popupSlide.hpp"
#include "motionPrefs.hpp"
#include "../genie/GenieGhost.hpp"

#include <QEasingCurve>
#include <QLabel>
#include <QEvent>
#include <QParallelAnimationGroup>
#include <QPointer>
#include <QPropertyAnimation>
#include <QTimer>
#include <QWidget>

namespace stencil::support {

  namespace {
    // `widget`'s picture poured between `at` and `home` (GLOBAL) on a ghost window of `host`; genie::fly
    // refuses, showing nothing, without a picture or a box.
    bool pourPicture(QWidget* host, QWidget& widget, const QRect& at, const QRect& home, bool opening,
                     std::function<void()> done = {}) {
      return genie::fly(host, widget.grab(), at, home, opening, /*asWindow=*/true, std::move(done), POPUP_GENIE_MS);
    }

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
        if (isSlideMotionOk() && pour(origin)) return;   // Transition grows it instead
        savedMin = popup->minimumSize();
        popup->setMinimumSize(1, 1);   // setGeometry clamps to it; settle() restores it
        const QRect first = slideStartRect(target, origin);
        popup->setGeometry(first);
        QEasingCurve curve(QEasingCurve::BezierSpline);
        curve.addCubicBezierSegment(QPointF(0.22, 0.61), QPointF(0.36, 1.0), QPointF(1.0, 1.0));   // menuFromAnchor's, unhurried from a speck
        auto* geo = new QPropertyAnimation(popup, "geometry", this);
        geo->setDuration(motionMs(POPUP_SLIDE_MS));
        geo->setStartValue(first);
        geo->setEndValue(target);
        geo->setEasingCurve(curve);
        auto* fade = new QPropertyAnimation(popup, "windowOpacity", this);
        fade->setDuration(motionMs(POPUP_SLIDE_MS));
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

      // The list stays veiled where it stands while its picture flies; a hide mid-flight unveils it.
      bool pour(const QPoint& origin) {
        QPointer<QWidget> list(popup);
        return pourPicture(popup, *popup, target, pourEdge(arrowBox(origin), target), /*opening=*/true,
                           [this, list, at = run] { if (list && at == run) list->setWindowOpacity(1.0); });
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

  // A hidden widget still renders, so the list is photographed as it closed; its ghost is a window of
  // the anchor's, never of the list it outlives.
  bool slidePopupOut(QWidget& popup, QWidget* anchor, const QPoint& originGlobal) {
    if (!anchor || !anchor->isVisible()) return false;
    const QRect at = popup.geometry();
    return pourPicture(anchor->window(), popup, at, pourEdge(arrowBox(originGlobal), at), /*opening=*/false);
  }

  bool shrinkPopupOut(QWidget& popup, const QPoint& originGlobal) {
    const QRect at = popup.geometry();
    const QPixmap shot = popup.grab();
    if (shot.isNull() || !at.isValid()) return false;
    auto* ghost = new QLabel(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowTransparentForInput
                                          | Qt::WindowDoesNotAcceptFocus | Qt::NoDropShadowWindowHint);
    ghost->setAttribute(Qt::WA_TranslucentBackground);
    ghost->setAttribute(Qt::WA_ShowWithoutActivating);
    ghost->setAttribute(Qt::WA_DeleteOnClose);
    ghost->setScaledContents(true);
    ghost->setPixmap(shot);
    ghost->setGeometry(at);
    ghost->show();
    const int ms = motionMs(POPUP_SHRINK_MS);
    auto* geo = new QPropertyAnimation(ghost, "geometry", ghost);
    geo->setDuration(ms);
    geo->setStartValue(at);
    geo->setEndValue(slideStartRect(at, originGlobal));
    auto* fade = new QPropertyAnimation(ghost, "windowOpacity", ghost);
    fade->setDuration(ms);
    fade->setKeyValueAt(0.0, 1.0);
    fade->setKeyValueAt(0.6, 0.85);
    fade->setKeyValueAt(1.0, 0.0);
    auto* both = new QParallelAnimationGroup(ghost);
    both->addAnimation(geo);
    both->addAnimation(fade);
    QObject::connect(both, &QParallelAnimationGroup::finished, ghost, &QWidget::close);
    both->start();
    return true;
  }

  namespace {
    // A short strip at the origin: the menu pours out of a point, as from under a title or the pointer.
    QRect menuOriginStrip(const QPoint& at) { return QRect(at.x() - 8, at.y() - 1, 16, 2); }
    // The window a menu belongs to outlives the menu (a context menu is deleted after its exec).
    QWidget* menuHost(QWidget* menu) { return menu->parentWidget() ? menu->parentWidget()->window() : menu; }
  }  // namespace

  bool pourMenuIn(QWidget* menu, const QPoint& originGlobal) {
    if (!menu || !isSlideMotionOk()) return false;
    QPointer<QWidget> guard(menu);
    return pourPicture(menu, *menu, menu->geometry(), menuOriginStrip(originGlobal), /*opening=*/true,
                       [guard] { if (guard) guard->setWindowOpacity(1.0); });
  }

  bool pourMenuOut(QWidget* menu, const QPoint& originGlobal) {
    if (!menu || !isSlideMotionOk() || !menu->isVisible()) return false;
    return pourPicture(menuHost(menu), *menu, menu->geometry(), menuOriginStrip(originGlobal), /*opening=*/false);
  }

}  // namespace stencil::support
