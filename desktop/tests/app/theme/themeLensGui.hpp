#pragma once
// Shared ground for the theme lens suites (MainWindow.themeLens*.gui.cpp): the switch, the lens and
// the wipe found on the live window, the stored settings read back, and the pointer path every
// drag suite drives (app/drag/iconDragGui.hpp).
#include "../../MainWindowPaint.gui.hpp"
#include "../drag/iconDragGui.hpp"

namespace stencil::guitest {

  inline QToolButton* switchFor(const QWidget& win, const QAction* act) {
    for (QToolButton* b : win.findChildren<QToolButton*>())
      if (b->defaultAction() == act && b->isVisible()) return b;
    return nullptr;
  }

  inline QWidget* lensOf(const QWidget& win) { return win.findChild<QWidget*>(QStringLiteral("themeLens")); }

  inline int wipesOn(const QWidget& win) {
    return win.findChildren<QWidget*>(QString::fromLatin1(ThemeSwapOverlay::OBJECT_NAME),
                                      Qt::FindDirectChildrenOnly).size();
  }

  inline QByteArray storedSettings() {
    QFile f(stencil::gui::fileStore::settingsPath());
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
  }

  inline void pressEscape(QWidget& win) {
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&win, &esc);
    QCoreApplication::processEvents();
  }

}  // namespace stencil::guitest
