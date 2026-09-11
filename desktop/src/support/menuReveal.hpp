#pragma once
#include <QPoint>

class QAction;
class QMenu;
class QMenuBar;
class QWidget;

// Grow-from-the-cursor pop for the custom context menus (canvas + chat card):
// the popup fades in (windowOpacity 0→1) while its rect grows out of the click
// point. Sibling of modalReveal's dialog flight, tuned for menus.
namespace stencil::support {

  // Menu/submenu dust clock (browser SURFACE_MENU_IN_MS ×1.5 — too brisk).
  inline constexpr int kMenuPopupDustMs = 340;
  // A SELECT's own popup reads slower next to the browser's — 1.5x this clock.
  inline constexpr int kSelectPopupDustMs = 510;

  // Call BEFORE exec()/popup() with the GLOBAL click point; the flight starts on
  // the menu's own Show, so it survives the blocking exec(). No-op headless
  // (offscreen platform) or with STENCIL_NO_ANIM=1.
  void revealMenu(QMenu& menu, const QPoint& origin);

  // The same sand for a popup that is NOT a QMenu and places itself: the list a
  // QComboBox drops (support/controlSwap.hpp). Call once the popup is on screen and
  // sized — its motes stream out of `anchor`, the control that owns it. Returns whether
  // the flight actually played; no-op headless or with STENCIL_NO_ANIM=1.
  bool revealPopup(QWidget& popup, QWidget* anchor, int ms = kMenuPopupDustMs);

  // …and the way back in: call before or after hiding the popup — grab() still renders a
  // hidden widget, which matters for a Qt::Popup Qt closed itself (an outside click).
  bool dismissPopup(QWidget& popup, QWidget* anchor, int ms = kMenuPopupDustMs);

  // …and a submenu ("Style ▸"): origin is read from `parentAction`'s row on `parent`
  // at Show time, not a fixed point. Call once, right after constructing `sub`.
  void revealSubmenu(QMenu& sub, QMenu& parent, QAction& parentAction);

  // A top-level menu-bar drop (File/Edit/Data/View/Project/Help): same re-arming as
  // revealSubmenu, but the origin is `menu`'s own cell on `bar`. No-op for a native
  // (OS-drawn) bar — there is no Qt window there to grab or fly.
  void revealMenuBarMenu(QMenu& menu, QMenuBar& bar);

  // Both edges for a menu that hangs off a CONTROL rather than a click point (the chat
  // composer's "…"): it forms out of `anchor` and pours back into it. Unlike revealMenu,
  // this survives repeated shows — such a menu is built once and popped many times — so
  // wire it once, right after the menu is built.
  void revealMenuFrom(QMenu& menu, QWidget* anchor);

}  // namespace stencil::support
