#include "MenuRowReveal.hpp"
#include "../../support/control/reveal/controlReveal.hpp"

#include <QActionEvent>
#include <QApplication>
#include <QMenu>
#include <QScreen>
#include <QVariantAnimation>
#include <QWidgetAction>

#include <algorithm>

namespace stencil::gui {

  namespace {
    constexpr const char* SLIDE_PROPERTY = "stencilMenuRowSlide";
    constexpr const char* HOME_Y_PROPERTY = "stencilMenuHomeY";

    QMenu* menuOf(QWidgetAction* a) {
      for (QObject* o : a->associatedObjects())
        if (auto* m = qobject_cast<QMenu*>(o); m && m->isVisible()) return m;
      return nullptr;
    }

    // QMenu measures a hosted row only when told the action changed. It is then placed against
    // where Qt first put it (beside its row): lifted while it would run off screen, settling back
    // as it shrinks (browser contextMenu/nav.js keepInView).
    void refit(QMenu* m, QWidgetAction* a) {
      if (!m->property(HOME_Y_PROPERTY).isValid()) {
        m->setProperty(HOME_Y_PROPERTY, m->y());
        QObject::connect(m, &QMenu::aboutToHide, m, [m] { m->setProperty(HOME_Y_PROPERTY, QVariant()); },
                         Qt::SingleShotConnection);
      }
      QActionEvent changed(QEvent::ActionChanged, a);
      QApplication::sendEvent(m, &changed);
      m->adjustSize();
      const QRect avail = m->screen() ? m->screen()->availableGeometry() : QRect();
      int y = m->property(HOME_Y_PROPERTY).toInt();
      if (avail.isValid()) y = std::max(avail.top(), std::min(y, avail.bottom() + 1 - m->height()));
      if (y != m->y()) m->move(m->x(), y);
    }

    void slide(QWidgetAction* a, QMenu* m, bool show) {
      QWidget* row = a->defaultWidget();
      if (auto* old = qobject_cast<QVariantAnimation*>(row->property(SLIDE_PROPERTY).value<QObject*>())) old->stop();
      row->setMaximumHeight(QWIDGETSIZE_MAX);
      const int full = row->sizeHint().height();
      const int ms = show ? CONTROL_REVEAL_IN_MS : CONTROL_REVEAL_OUT_MS;
      row->resize(m->width(), full);
      const QPixmap pm = ctl::groupShot(row);
      if (show) {
        row->setMaximumHeight(0);
        a->setVisible(true);
        refit(m, a);
      }
      // The cloud forms where the row will stand, or falls from where it stood.
      ctl::flyReveal(row, pm, QRect(row->mapTo(m, QPoint(0, 0)), QSize(row->width(), full)), show, ms);
      QGraphicsOpacityEffect* veil = veilBehindDust(row);   // while the motes fly, they ARE the row
      auto* anim = new QVariantAnimation(row);
      anim->setDuration(ms);
      anim->setStartValue(show ? 0 : full);
      anim->setEndValue(show ? full : 0);
      anim->setEasingCurve(QEasingCurve::InOutCubic);
      row->setProperty(SLIDE_PROPERTY, QVariant::fromValue<QObject*>(anim));
      // A stopped slide is deleted later, once its successor holds the property: it clears only its own.
      QObject::connect(anim, &QObject::destroyed, row, [row, self = static_cast<QObject*>(anim)] {
        if (row->property(SLIDE_PROPERTY).value<QObject*>() == self) row->setProperty(SLIDE_PROPERTY, QVariant());
      });
      QPointer<QMenu> menu(m);
      QPointer<QGraphicsOpacityEffect> veilGuard(veil);
      QObject::connect(anim, &QVariantAnimation::valueChanged, row, [row, a, menu, veilGuard, anim, show](const QVariant& v) {
        row->setMaximumHeight(v.toInt());
        if (veilGuard) veilGuard->setOpacity(show ? anim->currentTime() / double(anim->duration()) : 0.0);
        if (menu) refit(menu, a);
      });
      QObject::connect(anim, &QVariantAnimation::finished, row, [row, a, menu, show] {
        row->setGraphicsEffect(nullptr);
        row->setMaximumHeight(QWIDGETSIZE_MAX);
        if (!show) a->setVisible(false);
        if (menu) refit(menu, a);
      });
      anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
  }  // namespace

  void revealMenuRows(const QList<QWidgetAction*>& rows, bool show) {
    for (QWidgetAction* a : rows) {
      if (!a || !a->defaultWidget()) continue;
      QMenu* m = menuOf(a);
      if (!m || support::motionReduced()) {
        a->setVisible(show);
        if (m) refit(m, a);
        continue;
      }
      // A row mid-flight turns round even while it still counts as shown.
      const bool flying = a->defaultWidget()->property(SLIDE_PROPERTY).value<QObject*>() != nullptr;
      if (a->isVisible() == show && !flying) continue;
      slide(a, m, show);
    }
  }

}  // namespace stencil::gui
