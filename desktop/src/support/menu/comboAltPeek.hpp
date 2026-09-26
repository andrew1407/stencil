#pragma once
// Alt+hover peeks any selector's list (browser ui/tip/altPeek.js over control/customSelect.js):
// one filter on qApp drives an AltPeekGesture per QComboBox, SearchComboBox included, so no
// selector is wired by hand.
class QComboBox;
class QWidget;

namespace stencil::support {

  void installComboAltPeek();
  // Alt is down: the modifier query, or the Key_Alt events cocoa sends when the query lags.
  bool altKeyHeld();
  // The list window a combo is showing, or null when it is closed.
  QWidget* comboPopup(QComboBox* combo);
  // The popup list up now, when a widget inside `host` opened it.
  QWidget* popupOf(const QWidget* host);
  // …with the pointer over it, so `host` still has the pointer.
  bool pointerInPopupOf(const QWidget* host);

}  // namespace stencil::support
