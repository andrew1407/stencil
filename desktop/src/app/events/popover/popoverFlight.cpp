#include "popoverFlight.hpp"
#include "../../../support/genie/GenieGhost.hpp"
#include "../../../support/motionPrefs.hpp"

#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QPointer>
#include <QPropertyAnimation>
#include <QWidget>

namespace stencil::gui {

  namespace {
    QRect globalOf(QWidget* host, const QRect& r) { return QRect(host->mapToGlobal(r.topLeft()), r.size()); }

    // The overlay's box from `from` to `to` while `fx` holds it opaque for all but the shrink's tail (or the
    // grow's first frames), so it is seen growing out of the icon and shrinking all the way back into it.
    void morph(QWidget* overlay, QGraphicsOpacityEffect* fx, const QRect& from, const QRect& to, bool growing,
               int ms, std::function<void()> done = {}) {
      auto* box = new QPropertyAnimation(overlay, "geometry", overlay);
      box->setDuration(support::motionMs(ms));
      box->setStartValue(from);
      box->setEndValue(to);
      box->setEasingCurve(growing ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
      auto* fade = new QPropertyAnimation(fx, "opacity", overlay);
      fade->setDuration(support::motionMs(ms));
      fade->setKeyValueAt(0.0, growing ? 0.0 : 1.0);
      fade->setKeyValueAt(growing ? 0.08 : 0.8, 1.0);
      fade->setKeyValueAt(1.0, growing ? 1.0 : 0.0);
      if (done) QObject::connect(box, &QAbstractAnimation::finished, overlay, std::move(done));
      box->start(QAbstractAnimation::DeleteWhenStopped);
      fade->start(QAbstractAnimation::DeleteWhenStopped);
    }
  }  // namespace

  void popoverEntrance(QWidget* host, QWidget* overlay, QGraphicsOpacityEffect* fx, const QPixmap& shot,
                       const QRect& fromBox, const QRect& box, int ms) {
    QPointer<QGraphicsOpacityEffect> veil(fx);
    if (support::isSlideMotionOk() && !shot.isNull()
        && support::genie::fly(host, shot, globalOf(host, box), globalOf(host, fromBox), /*opening=*/true,
                               /*asWindow=*/false, [veil] { if (veil) veil->setOpacity(1.0); }))
      return;
    overlay->setGeometry(fromBox);
    morph(overlay, fx, fromBox, box, /*growing=*/true, ms);
  }

  void popoverExit(QWidget* host, QWidget* overlay, const QPixmap& shot, const QRect& fromBox, int ms,
                   std::function<void()> end) {
    if (support::isSlideMotionOk() && !shot.isNull()
        && support::genie::fly(host, shot, globalOf(host, overlay->geometry()), globalOf(host, fromBox),
                               /*opening=*/false, /*asWindow=*/false, end)) {
      overlay->hide();   // its picture flies; the ghost was painted before this
      return;
    }
    auto* fx = qobject_cast<QGraphicsOpacityEffect*>(overlay->graphicsEffect());
    if (!fx) {
      fx = new QGraphicsOpacityEffect(overlay);
      overlay->setGraphicsEffect(fx);
    }
    fx->setOpacity(1.0);
    morph(overlay, fx, overlay->geometry(), fromBox, /*growing=*/false, ms, std::move(end));
  }

}  // namespace stencil::gui
