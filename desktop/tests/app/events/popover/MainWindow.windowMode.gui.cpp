// MainWindow GUI e2e — switching Multiple windows while a window is open (PopoverGesturesSideBySide.cpp;
// browser twin ui/modal/registry.js): the open window is re-shown under the new mode's modality and its
// caller keeps waiting; switched off, the others close and the one kept blocks the app again under its
// backdrop. Shared ground: MainWindow.gui.hpp.
#include "../../../MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  static void setMulti(MainWindow& win, bool on) {
    Settings s = win.settings;
    s.multiWindow = on;
    win.applySettings(s, /*persist=*/false);
  }
  static QDialog* openWindow(MainWindow& win) {
    for (const QPointer<QDialog>& d : win.pop.windows)
      if (d && d->isVisible()) return d.data();
    return nullptr;
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void anOpenWindowFollowsTheModeBothWays() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    setMulti(win, false);
    bool modalAtOpen = false, sideBySide = false, stillOpen = false, modalAgain = false;
    QTimer::singleShot(200, &win, [&] {
      QDialog* dlg = openWindow(win);
      if (!dlg) return;
      modalAtOpen = dlg->windowModality() == Qt::ApplicationModal;
      setMulti(win, true);
      sideBySide = dlg->windowModality() == Qt::NonModal && dlg->isVisible();
      stillOpen = openWindow(win) == dlg;
      setMulti(win, false);
      QTest::qWait(700);   // the others' flight home, then the kept window's re-show
      modalAgain = dlg->windowModality() == Qt::ApplicationModal && dlg->isVisible();
      dlg->reject();
    });
    win.acts.info->trigger();   // the caller waits in the window's loop through every switch
    QVERIFY2(modalAtOpen, "one window at a time opens it application-modal");
    QVERIFY2(sideBySide && stillOpen, "switched on, the same window is re-shown non-modal and stays");
    QVERIFY2(modalAgain, "switched off, the window kept blocks the app again");
    QVERIFY(win.pop.windows.isEmpty());
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.windowMode.gui.moc"
