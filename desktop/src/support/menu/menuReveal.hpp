#pragma once
#include <QPoint>

class QAction;
class QMenu;
class QMenuBar;
class QWidget;

// Grow-from-the-cursor pop for the custom context menus; sibling of modalReveal's flight.
namespace stencil::support {

  // Browser SURFACE_MENU_IN_MS ×1.5.
  inline constexpr int MENU_POPUP_DUST_MS = 340;
  // What a menu was armed with, so a test can read the clock back off it.
  inline constexpr const char* DUST_MS_PROP = "stencilDustMs";
  // A SELECT's popup — and the canvas context menu with its nested submenus — run 1.5x
  // this clock, opening and closing alike.
  inline constexpr int SELECT_POPUP_DUST_MS = 510;
  inline constexpr int CONTEXT_MENU_DUST_MS = 510;
  // A flyout opens this far past its parent's edge, never over it (browser contextMenu/model.js SUB_GAP).
  inline constexpr int SUBMENU_GAP = 2;

  // Call BEFORE exec()/popup() with the GLOBAL click point; the flight starts on the
  // menu's own Show, so it survives the blocking exec(). No-op headless / STENCIL_NO_ANIM.
  void revealMenu(QMenu& menu, const QPoint& origin, int ms = MENU_POPUP_DUST_MS);

  // For a popup that is NOT a QMenu (a QComboBox's list): call once it is on screen and sized.
  bool revealPopup(QWidget& popup, QWidget* anchor, int ms = MENU_POPUP_DUST_MS);

  // Call before or after hiding: grab() still renders a hidden widget.
  bool dismissPopup(QWidget& popup, QWidget* anchor, int ms = MENU_POPUP_DUST_MS);

  // Origin is read from `parentAction`'s row at Show time. Call once, right after constructing `sub`.
  void revealSubmenu(QMenu& sub, QMenu& parent, QAction& parentAction,
                     int ms = MENU_POPUP_DUST_MS);

  // No-op for a native (OS-drawn) bar — nothing Qt-rendered to grab.
  void revealMenuBarMenu(QMenu& menu, QMenuBar& bar);

  // For a menu hanging off a CONTROL: survives repeated shows, so wire it once after building.
  void revealMenuFrom(QMenu& menu, QWidget* anchor);

}  // namespace stencil::support
