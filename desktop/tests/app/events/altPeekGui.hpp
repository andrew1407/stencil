#pragma once
// Shared ground for the selector Alt-peek GUI cases (MainWindow.altPeek*.gui.cpp): Alt arrives
// the way cocoa delivers it — a Key_Alt event to whatever holds the focus — and each case runs
// under the particles, slide and no-motion modes.
#include "../../MainWindow.gui.hpp"
#include "../../../src/support/menu/comboAltPeek.hpp"
#include "../../../src/support/tip/altPeek.hpp"
#include "../../../src/support/motionPrefs.hpp"

namespace stencil::guitest {

  using stencil::support::comboPopup;
  using stencil::support::MotionMode;

  inline void addMotionRows() {
    QTest::addColumn<int>("mode");
    QTest::newRow("particles") << int(MotionMode::PARTICLES);
    QTest::newRow("slide") << int(MotionMode::SLIDE);
    QTest::newRow("none") << int(MotionMode::NONE);
  }

  inline void altKey(QEvent::Type t) {
    QWidget* to = QApplication::focusWidget();
    if (!to) to = QApplication::activePopupWidget();
    if (!to) to = QApplication::activeWindow();
    if (!to) QFAIL("nothing to deliver the Alt key to");
    QKeyEvent ev(t, Qt::Key_Alt, t == QEvent::KeyPress ? Qt::AltModifier : Qt::NoModifier);
    static quint64 stamp = 1;
    ev.setTimestamp(++stamp);   // every real key event carries its own time
    QApplication::sendEvent(to, &ev);
  }

  inline void enterWidget(QWidget* w) {
    const QPointF at(w->width() / 2.0, w->height() / 2.0);
    QEnterEvent ev(at, at, w->mapToGlobal(at));
    QApplication::sendEvent(w, &ev);
  }

  inline QPoint centreOf(const QWidget* w) { return w->mapToGlobal(w->rect().center()); }

  // A real display without Accessibility rights ignores QCursor::setPos; the cases that place
  // the pointer skip there rather than read the user's own.
  inline bool cursorWarps(const QWidget& win) {
    const QPoint to = win.mapToGlobal(QPoint(win.width() / 3, win.height() / 2));
    QCursor::setPos(to);
    const bool moved = QTest::qWaitFor([&] { return QCursor::pos() == to; }, 300);
    QTest::qWait(50);   // …and the enter/leave it causes land before the case goes on
    return moved;
  }

  inline QComboBox* firstCombo(QWidget* in) {
    for (QComboBox* c : in->findChildren<QComboBox*>())
      if (c->isVisible() && c->isEnabled()) return c;
    return nullptr;
  }

  // A text field of the window's own, so the key route is tried with the focus in a field.
  inline QWidget* focusAField(QWidget* in) {
    for (QAbstractSpinBox* f : in->findChildren<QAbstractSpinBox*>())
      if (f->isVisible() && f->isEnabled()) {
        f->setFocus(Qt::OtherFocusReason);
        return f;
      }
    return nullptr;
  }

  // Nothing of a closed list or popover may stay on screen: no window beside the editor.
  inline QStringList strayWindows(const QWidget& win) {
    QStringList out;
    for (QWidget* w : QApplication::topLevelWidgets())
      if (w != &win && w->isVisible() && !dynamic_cast<stencil::gui::AppTooltip*>(w))
        out << QStringLiteral("%1(%2)").arg(w->metaObject()->className(), w->objectName());
    return out;
  }

  inline void activate(QWidget& win) {
    win.windowHandle()->requestActivate();
    (void)QTest::qWaitFor([&] { return QGuiApplication::focusWindow() == win.windowHandle(); });
  }

  // Between steps: a list just closed ignores a reopen for 150 ms (SearchComboBox's click
  // toggle), and offscreen hands a shown popup the activation and keeps it.
  inline void reactivate(QWidget& win) {
    QTest::qWait(200);
    activate(win);
  }

  // A window loaded with a picture, active, motion on and set to `mode`.
  inline void bootForPeeks(MainWindow& win, MotionMode mode) {
    win.resize(1200, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    activate(win);
    QImage pic(320, 240, QImage::Format_RGB32);
    pic.fill(Qt::darkCyan);
    CanvasWidget* canvas = win.findChild<CanvasWidget*>();
    QVERIFY(canvas);
    canvas->loadFromImage(pic);
    QTRY_VERIFY(canvas->hasImage());
    stencil::support::setMotionMode(mode);
  }

}  // namespace stencil::guitest
