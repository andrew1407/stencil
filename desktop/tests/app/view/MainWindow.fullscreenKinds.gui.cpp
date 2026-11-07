// MainWindow GUI e2e — The two fullscreens: Full Screen (Alt+F, a click) takes the screen, Fullscreen in
// Window (Alt+Shift+F, a double-click) hides the chrome inside the window as it is; right-click opens a
// menu of both (browser ui/fullscreen/button.js). Shared ground is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

#include "uiTimings.hpp"

#include <QContextMenuEvent>
#include <QMenu>

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  static QToolButton* fullscreenButton(MainWindow& win) {
    for (QToolButton* b : win.findChildren<QToolButton*>())
      if (b->defaultAction() == win.acts.fullscreen) return b;
    return nullptr;
  }
  // What the platform delivers for one button press-and-release; a double-click's second press is a DblClick.
  static void send(QWidget* w, QEvent::Type t) {
    const QPointF at(4, 4);
    QMouseEvent e(t, at, w->mapToGlobal(at), Qt::LeftButton,
                  t == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(w, &e);
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void theHotkeysAreAltFAndAltShiftF() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QCOMPARE(win.acts.fullscreen->shortcut(), QKeySequence(QStringLiteral("Alt+F")));
    QCOMPARE(win.acts.fullscreenWindow->shortcut(), QKeySequence(QStringLiteral("Alt+Shift+F")));
    QCOMPARE(win.acts.flipImage->shortcut(), QKeySequence(QStringLiteral("Alt+Shift+H")));
    QVERIFY2(win.acts.fullscreen->toolTip().contains(QStringLiteral("Double-click")), "the tooltip names the double-click");
  }

  void inWindowKeepsTheWindowAndHidesTheChrome() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.acts.fullscreenWindow->trigger();
    QVERIFY(win.fs.active && win.fs.inWindow);
    QVERIFY(!win.tools.headerToolbar->isVisible());
    QTest::qWait(50);
    QVERIFY2(!(win.windowState() & Qt::WindowFullScreen), "in-window fullscreen must not take the screen");
    QCOMPARE(win.acts.fullscreenWindow->text(), QStringLiteral("Exit Fullscreen in Window"));
    win.acts.fullscreenWindow->trigger();
    QVERIFY(!win.fs.active);
    QVERIFY(win.tools.headerToolbar->isVisible());
  }

  void theOtherKindSwitchesWithoutLeaving() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.acts.fullscreenWindow->trigger();
    win.acts.fullscreen->trigger();
    QVERIFY(win.fs.active && !win.fs.inWindow);
    QVERIFY(!win.tools.headerToolbar->isVisible());
    QTRY_VERIFY(win.windowState() & Qt::WindowFullScreen);
    win.acts.fullscreenWindow->trigger();
    QVERIFY(win.fs.active && win.fs.inWindow);
    QTRY_VERIFY(!(win.windowState() & Qt::WindowFullScreen));
    QVERIFY2(win.fs.active, "the system's state change from our own switch is not an exit");
    win.parts.view.toggleFullscreen();   // the Escape path: off, whichever kind
    QVERIFY(!win.fs.active);
  }

  void clickIsTheScreenDoubleClickTheWindow() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QToolButton* btn = fullscreenButton(win);
    QVERIFY(btn);
    send(btn, QEvent::MouseButtonPress);
    send(btn, QEvent::MouseButtonRelease);
    QVERIFY2(!win.fs.active, "a click waits out the double-click interval");
    QTRY_VERIFY(win.fs.active);
    QVERIFY(!win.fs.inWindow);
    win.parts.view.toggleFullscreen();
    QTRY_VERIFY(!win.fs.active);
    QTRY_VERIFY(!(win.windowState() & Qt::WindowFullScreen));   // the screen's own state lands late under load

    for (QEvent::Type t : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease, QEvent::MouseButtonDblClick,
                           QEvent::MouseButtonRelease})
      send(btn, t);
    QVERIFY(win.fs.active && win.fs.inWindow);
    QTest::qWait(stencil::support::uiTimings().doubleClickMs + 50);
    QVERIFY2(win.fs.active && win.fs.inWindow, "the double never also fires the single");
    win.parts.view.toggleFullscreen();
  }

  // A held press opens the menu; its release picks the row under it, or closes the menu anywhere else.
  void holdThenReleasePicksTheRowUnderIt() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QToolButton* btn = fullscreenButton(win);
    QVERIFY(btn);
    for (const bool onRow : {false, true}) {
      send(btn, QEvent::MouseButtonPress);
      QTest::qWait(stencil::support::uiTimings().longPressMs + 80);
      QMenu* menu = win.parts.view.fsMenu;
      QVERIFY(menu);
      QTRY_VERIFY(menu->isVisible());
      const QPointF at = onRow ? QPointF(menu->actionGeometry(menu->actions().at(1)).center()) : QPointF(-40, -40);
      QMouseEvent up(QEvent::MouseButtonRelease, at, menu->mapToGlobal(at), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(menu, &up);
      QTRY_VERIFY(!menu->isVisible());
      QTest::qWait(30);
      QCOMPARE(win.fs.active, onRow);   // off the list: closed, nothing chosen
      if (onRow) QVERIFY(win.fs.inWindow);
      send(btn, QEvent::MouseButtonRelease);   // the button's own copy of the release is swallowed
      QTest::qWait(stencil::support::uiTimings().doubleClickMs + 50);
      QCOMPARE(win.fs.active, onRow);
    }
    win.parts.view.toggleFullscreen();
  }


  void rightClickOpensBothKinds() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QToolButton* btn = fullscreenButton(win);
    QVERIFY(btn);
    QContextMenuEvent ev(QContextMenuEvent::Mouse, QPoint(4, 4), btn->mapToGlobal(QPoint(4, 4)));
    QApplication::sendEvent(btn, &ev);
    QMenu* menu = win.parts.view.fsMenu;
    QVERIFY(menu);
    QTRY_VERIFY(menu->isVisible());
    const QList<QAction*> rows = menu->actions();
    QCOMPARE(rows.size(), 2);
    QVERIFY(rows[0]->text().startsWith(QStringLiteral("Full Screen")));
    QVERIFY(rows[1]->text().startsWith(QStringLiteral("Fullscreen in Window")));
    QVERIFY2(!rows[0]->icon().isNull() && !rows[1]->icon().isNull(), "each row carries its glyph");
    QVERIFY(!win.fs.active);
    rows[1]->trigger();
    menu->hide();
    QTRY_VERIFY(win.fs.active && win.fs.inWindow);
    win.parts.view.toggleFullscreen();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.fullscreenKinds.gui.moc"
