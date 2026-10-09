#pragma once
// The accent popover suites' common ground: a window holding a picture, so a canvas press is an
// ordinary editing press and not the empty canvas's "create a blank image" invitation, and the
// toolbar button an action is on.
#include "../../MainWindow.gui.hpp"

namespace stencil::guitest {

  // Shown with a flat picture loaded and no typing focus (the typingFocus gate off).
  inline bool showWithPicture(MainWindow& win) {
    win.resize(1000, 700);
    win.show();
    if (!QTest::qWaitForWindowExposed(&win)) return false;
    QImage pic(320, 240, QImage::Format_RGB32);
    pic.fill(Qt::darkCyan);
    auto* canvas = win.findChild<CanvasWidget*>();
    canvas->loadFromImage(pic);
    if (!QTest::qWaitFor([canvas] { return canvas->hasImage(); }, 5000)) return false;
    if (QWidget* fw = QApplication::focusWidget()) fw->clearFocus();
    return true;
  }

  // The visible toolbar button carrying `action`, out of the popover host's button map.
  inline QToolButton* visibleButtonFor(const QHash<QObject*, QAction*>& buttons, QAction* action) {
    QToolButton* found = nullptr;
    for (auto it = buttons.cbegin(); it != buttons.cend(); ++it)
      if (it.value() == action && static_cast<QWidget*>(it.key())->isVisible())
        found = static_cast<QToolButton*>(it.key());
    return found;
  }

}  // namespace stencil::guitest
