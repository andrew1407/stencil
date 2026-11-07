#include "menuReveal.hpp"
#include "menuRevealFilters.hpp"
#include "popupSlide.hpp"

#include <QComboBox>
#include <QGuiApplication>
#include <QCursor>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPointer>
#include <algorithm>

namespace stencil::support {

  namespace {
    // A COMBO's motes leave from the caret at its right edge (browser dropdownMenu.js
    // dustPoint parity); everything else keeps its centre.
    // A menu's reveal is installed as it is built and picks dust, the Sliding pour or nothing as it
    // opens, so a mode chosen later still moves it. Never on the compositor-less offscreen platform.
    bool menuMotionPossible() {
      return QGuiApplication::platformName() != QLatin1String("offscreen") && !showMotionReduced();
    }

    // A press on the trigger closes the menu and is swallowed there, so its click cannot reopen it.
    class TriggerPressGuard : public QObject {
     public:
      TriggerPressGuard(QMenu* menu, std::function<QRect()> zone) : QObject(menu), menu(menu), zone(std::move(zone)) {
        setObjectName(QStringLiteral("stencilTriggerPressGuard"));
        menu->installEventFilter(this);
      }

     protected:
      bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched != menu) return false;
        if (event->type() == QEvent::Show) menu->setAttribute(Qt::WA_NoMouseReplay, false);
        if (event->type() != QEvent::MouseButtonPress) return false;
        const QPoint at = static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
        if (!menu->geometry().contains(at) && zone().contains(at)) menu->setAttribute(Qt::WA_NoMouseReplay, true);
        return false;
      }

     private:
      QMenu* menu;
      std::function<QRect()> zone;
    };

    QPoint popupOriginGlobal(QWidget* anchor) {
      const QRect r = anchor->rect();
      if (qobject_cast<QComboBox*>(anchor))
        return anchor->mapToGlobal(
            QPoint(std::max(r.center().x(), r.right() - 14), r.center().y()));
      return anchor->mapToGlobal(r.center());
    }
  }  // namespace

  QPoint submenuArrowPoint(const QMenu& parent, QAction* action) {
    const QRect row = action ? parent.actionGeometry(action) : QRect();
    if (!row.isValid()) return {};
    // Qt's style draws the ▸ in a (h - 4) / 2 square, 3px in from the row's right edge.
    const int dim = std::max(4, (row.height() - 4) / 2);
    return parent.mapToGlobal(QPoint(row.right() - 3 - dim / 2, row.center().y()));
  }

  bool revealPopup(QWidget& popup, QWidget* anchor, int ms) {
    if (!anchor || !anchor->isVisible()) return false;
    if (isSlideMotionOk() || isTransitionMotionOk()) {   // poured in Sliding, grown in Transition
      slidePopupIn(popup, popupOriginGlobal(anchor));
      return true;
    }
    if (!isDustMotionOk()) return false;
    QWidget* host = anchor->window();
    return dustPopupIn(&popup, host, popupOriginGlobal(anchor), ms);
  }

  bool dismissPopup(QWidget& popup, QWidget* anchor, int ms) {
    if (isSlideMotionOk()) return anchor && slidePopupOut(popup, anchor, popupOriginGlobal(anchor));
    if (isTransitionMotionOk()) return anchor && anchor->isVisible() && shrinkPopupOut(popup, popupOriginGlobal(anchor));
    if (!isDustMotionOk()) return false;
    if (!anchor || !anchor->isVisible()) return false;
    // No isVisible() gate: a Qt::Popup Qt closed itself is already hidden by its Hide event.
    return dustPopupOut(&popup, anchor->window(), popupOriginGlobal(anchor), ms);
  }

  void revealMenu(QMenu& menu, const QPoint& origin, int ms) {
    menu.setProperty(DUST_MS_PROP, ms);
    if (!menuMotionPossible()) return;   // offscreen has no compositor for windowOpacity
    new MenuReveal(&menu, [origin] { return origin; }, ms);  // owned by the menu
  }

  void revealSubmenu(QMenu& sub, QMenu& parent, QAction& parentAction, int ms) {
    new SubmenuCloseGuard(&sub, &parent, &parentAction);   // owned by sub
    sub.setProperty(DUST_MS_PROP, ms);
    if (!menuMotionPossible()) return;
    QPointer<QMenu> parentGuard(&parent);
    QPointer<QAction> actionGuard(&parentAction);
    new MenuReveal(&sub, [parentGuard, actionGuard] {
      if (!parentGuard || !actionGuard) return QCursor::pos();
      const QPoint at = submenuArrowPoint(*parentGuard, actionGuard);
      return at.isNull() ? QCursor::pos() : at;
    }, ms);
  }

  void revealMenuBarMenu(QMenu& menu, QMenuBar& bar) {
    menu.setProperty(DUST_MS_PROP, MENU_POPUP_DUST_MS);
    if (!menuMotionPossible()) return;
    if (bar.isNativeMenuBar()) return;   // natively drawn: nothing Qt-rendered to grab
    QPointer<QMenuBar> barGuard(&bar);
    QPointer<QMenu> menuGuard(&menu);
    new MenuReveal(&menu, [barGuard, menuGuard] {
      if (!barGuard || !menuGuard) return QCursor::pos();
      const QRect cell = barGuard->actionGeometry(menuGuard->menuAction());
      return cell.isValid() ? barGuard->mapToGlobal(cell.center()) : QCursor::pos();
    }, MENU_POPUP_DUST_MS);
  }

  void revealMenuFrom(QMenu& menu, QWidget* anchor) {
    if (!anchor || menu.findChild<QObject*>(QStringLiteral("stencilMenuFlight"), Qt::FindDirectChildrenOnly)) return;
    new MenuFlight(&menu, anchor);  // owned by the menu
    QPointer<QWidget> guard(anchor);
    closeOnTriggerPress(menu, [guard] {
      return guard && guard->isVisible() ? QRect(guard->mapToGlobal(QPoint(0, 0)), guard->size()) : QRect();
    });
  }

  void closeOnTriggerPress(QMenu& menu, std::function<QRect()> globalZone) {
    new TriggerPressGuard(&menu, std::move(globalZone));   // owned by the menu
  }

}  // namespace stencil::support
