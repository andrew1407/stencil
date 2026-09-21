#include "menuReveal.hpp"
#include "menuRevealFilters.hpp"

#include <QComboBox>
#include <QCursor>
#include <QMenuBar>
#include <QPointer>
#include <algorithm>

namespace stencil::support {

  namespace {
    // A COMBO's motes leave from the caret at its right edge (browser dropdownMenu.js
    // dustPoint parity); everything else keeps its centre.
    QPoint popupOriginGlobal(QWidget* anchor) {
      const QRect r = anchor->rect();
      if (qobject_cast<QComboBox*>(anchor))
        return anchor->mapToGlobal(
            QPoint(std::max(r.center().x(), r.right() - 14), r.center().y()));
      return anchor->mapToGlobal(r.center());
    }
  }  // namespace

  bool revealPopup(QWidget& popup, QWidget* anchor, int ms) {
    if (!isDustMotionOk()) return false;
    if (!anchor || !anchor->isVisible()) return false;
    QWidget* host = anchor->window();
    return dustPopupIn(&popup, host, popupOriginGlobal(anchor), ms);
  }

  bool dismissPopup(QWidget& popup, QWidget* anchor, int ms) {
    if (!isDustMotionOk()) return false;
    if (!anchor || !anchor->isVisible()) return false;
    // No isVisible() gate: a Qt::Popup Qt closed itself is already hidden by its Hide event.
    return dustPopupOut(&popup, anchor->window(), popupOriginGlobal(anchor), ms);
  }

  void revealMenu(QMenu& menu, const QPoint& origin, int ms) {
    menu.setProperty(DUST_MS_PROP, ms);
    if (!isDustMotionOk()) return;   // offscreen has no compositor for windowOpacity
    new MenuReveal(&menu, [origin] { return origin; }, ms);  // owned by the menu
  }

  void revealSubmenu(QMenu& sub, QMenu& parent, QAction& parentAction, int ms) {
    new SubmenuCloseGuard(&sub, &parent, &parentAction);   // owned by sub
    sub.setProperty(DUST_MS_PROP, ms);
    if (!isDustMotionOk()) return;
    QPointer<QMenu> parentGuard(&parent);
    QPointer<QAction> actionGuard(&parentAction);
    // The row's ▸ caret at its RIGHT edge (browser contextMenu.js subPoint parity).
    new MenuReveal(&sub, [parentGuard, actionGuard] {
      if (!parentGuard || !actionGuard) return QCursor::pos();
      const QRect row = parentGuard->actionGeometry(actionGuard);
      return row.isValid() ? parentGuard->mapToGlobal(QPoint(row.right(), row.center().y()))
                           : QCursor::pos();
    }, ms);
  }

  void revealMenuBarMenu(QMenu& menu, QMenuBar& bar) {
    menu.setProperty(DUST_MS_PROP, MENU_POPUP_DUST_MS);
    if (!isDustMotionOk()) return;
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
    if (!anchor) return;
    new MenuFlight(&menu, anchor);  // owned by the menu
  }

}  // namespace stencil::support
