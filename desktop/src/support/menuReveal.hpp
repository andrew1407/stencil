#pragma once
#include <QPoint>

class QAction;
class QMenu;
class QMenuBar;
class QWidget;

// Grow-from-the-cursor pop for the custom context menus; sibling of modalReveal's flight.
namespace stencil::support {

  // Browser SURFACE_MENU_IN_MS ×1.5.
  inline constexpr int kMenuPopupDustMs = 340;
  // A SELECT's popup runs 1.5x this clock.
  inline constexpr int kSelectPopupDustMs = 510;

  // Call BEFORE exec()/popup() with the GLOBAL click point; the flight starts on the
  // menu's own Show, so it survives the blocking exec(). No-op headless / STENCIL_NO_ANIM.
  void revealMenu(QMenu& menu, const QPoint& origin);

  // For a popup that is NOT a QMenu (a QComboBox's list): call once it is on screen and sized.
  bool revealPopup(QWidget& popup, QWidget* anchor, int ms = kMenuPopupDustMs);

  // Call before or after hiding: grab() still renders a hidden widget.
  bool dismissPopup(QWidget& popup, QWidget* anchor, int ms = kMenuPopupDustMs);

  // Origin is read from `parentAction`'s row at Show time. Call once, right after constructing `sub`.
  void revealSubmenu(QMenu& sub, QMenu& parent, QAction& parentAction);

  // No-op for a native (OS-drawn) bar — nothing Qt-rendered to grab.
  void revealMenuBarMenu(QMenu& menu, QMenuBar& bar);

  // For a menu hanging off a CONTROL: survives repeated shows, so wire it once after building.
  void revealMenuFrom(QMenu& menu, QWidget* anchor);

}  // namespace stencil::support
