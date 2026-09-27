// MainWindow GUI e2e — Where an unanchored dialog grows from: the icon a shortcut stands for,
// from above when that icon is folded away, and the pointer after a press. Shared ground is in
// MainWindow.gui.hpp; the anchor itself is support/modal/modalReveal.cpp gestureAnchorRect.
#include "../../MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void aShortcutsDialogGrowsFromItsIcon() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.activateWindow();
    openLoaded(win);
    QAction* act = win.acts.zoomIn;   // an action with a toolbar icon and no window of its own
    QWidget* icon = win.buttonForAction(act);
    QVERIFY2(icon, "zoom-in has no visible toolbar icon");
    // A shortcut as the app meets it: a key reaches the window, then the action fires.
    const auto viaKeyboard = [&] {
      QTest::keyClick(&win, Qt::Key_Shift);
      act->trigger();
    };

    viaKeyboard();
    QCOMPARE(stencil::support::gestureAnchorRect(), QRect(icon->mapToGlobal(QPoint(0, 0)), icon->size()));

    win.parts.view.setToolbarsShown(false, /*animate=*/false);
    viaKeyboard();
    QVERIFY2(!stencil::support::gestureAnchorRect().isValid(),
             "with the rows folded the flight must fall from above, not from a stale icon");

    QTest::mousePress(win.canvas, Qt::LeftButton, {}, QPoint(20, 20));
    QTest::mouseRelease(win.canvas, Qt::LeftButton, {}, QPoint(20, 20));
    const QRect afterPress = stencil::support::gestureAnchorRect();
    QVERIFY2(afterPress.isValid() && afterPress.contains(QCursor::pos()), "a press takes the origin back");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.gestureAnchor.gui.moc"
