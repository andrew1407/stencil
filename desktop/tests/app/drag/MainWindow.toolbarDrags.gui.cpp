// MainWindow GUI e2e — Dragging the toolbar's icons (app/drag): a dialog icon dropped away opens its
// full dialog with its top-left on the drop, kept on the screen, released back on itself it opens
// nothing; the chat icon docks on the band it drops on or floats there; no other icon drags.
// Shared ground is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "iconDragGui.hpp"
#include "iconDrag.hpp"
#include "uiTimings.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private:
  static QRect screenOf(const MainWindow& win) { return win.screen()->availableGeometry(); }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  void aDialogIconDroppedAwayOpensItsDialogWithItsTopLeftOnTheDrop() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(openLoaded(win) && win.canvas->hasImage());
    settleLayout(&win, 150);
    const QPoint drop = win.scroll->mapToGlobal(QPoint(win.scroll->width() / 3, win.scroll->height() / 3));
    const QPoint hostMid = win.mapToGlobal(QPoint(win.width() / 2, win.height() / 2));
    int moved = 0;
    for (QAction* act : {win.acts.settings, win.acts.shortcuts, win.acts.info, win.acts.projects,
                         win.acts.connect, win.acts.script, win.acts.crop, win.acts.openAnother}) {
      QWidget* icon = win.buttonForAction(act);
      QVERIFY2(icon && icon->isEnabled(), qPrintable(act->text()));
      ModalSeen seen;
      catchModal(seen);
      dragIcon(icon, drop, drop);
      QTRY_VERIFY2(seen.seen, qPrintable(act->text() + " opened no dialog"));
      QVERIFY2(!win.pop.active, "the full dialog, never the compact popover");
      const QPoint landed = stencil::support::topLeftAt(drop, seen.frame.size(), screenOf(win));
      QVERIFY2(seen.frame.topLeft() == landed, qPrintable(act->text() + " does not open with its top-left on the drop"));
      moved += landed != stencil::support::centredTopLeft(hostMid, seen.frame.size(), screenOf(win));
      QTRY_VERIFY(!QApplication::activeModalWidget());
    }
    QVERIFY2(moved > 0, "every dialog would have sat on the window's centre anyway");
  }

  void theOpenImageButtonOpensWhereItDropsToo() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(win.tools.openImageBtn->isVisible());
    const QPoint drop = win.scroll->mapToGlobal(QPoint(120, 120));
    ModalSeen seen;
    catchModal(seen);
    dragIcon(win.tools.openImageBtn, drop, drop);
    QTRY_VERIFY(seen.seen);
    QCOMPARE(seen.frame.topLeft(), stencil::support::topLeftAt(drop, seen.frame.size(), screenOf(win)));
  }

  // The cancel clicks nothing: the popover icons' deferred click is never armed by it.
  void aDialogIconReleasedBackOnItselfOpensNothingButAClickStillOpens() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QWidget* icon = win.buttonForAction(win.acts.projects);
    QVERIFY(icon);
    ModalSeen seen;
    catchModal(seen, stencil::support::uiTimings().doubleClickMs + 400);
    dragIcon(icon, win.scroll->mapToGlobal(QPoint(200, 200)), iconCentre(icon));
    QTest::qWait(stencil::support::uiTimings().doubleClickMs + 300);
    QVERIFY2(!seen.seen && !win.pop.pendingAction, "released on its icon, the drag opened the dialog");
    ModalSeen clicked;
    catchModal(clicked);
    QTest::mouseClick(icon, Qt::LeftButton);
    QTRY_VERIFY2(clicked.seen, "a plain click no longer opens the dialog");
  }

  // A quick click, then a drag: the drag's press is a double-click's second, and still only drags.
  void aClickThenADragOpensOnlyTheFullDialogOnTheDrop() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QWidget* icon = win.buttonForAction(win.acts.projects);
    QVERIFY(icon);
    const QPoint drop = win.scroll->mapToGlobal(QPoint(180, 160));
    ModalSeen seen;
    catchModal(seen);
    iconMouse(icon, QEvent::MouseButtonPress, iconCentre(icon));
    iconMouse(icon, QEvent::MouseButtonRelease, iconCentre(icon));
    iconMouse(icon, QEvent::MouseButtonDblClick, iconCentre(icon));
    iconMouse(icon, QEvent::MouseMove, iconCentre(icon) + QPoint(0, 40));
    QVERIFY2(stencil::support::iconDragActive(icon) && !win.pop.active, "the double-click's popover took the drag");
    dropIcon(icon, drop);
    QTRY_VERIFY2(seen.seen, "the drop opened no dialog");
    QCOMPARE(seen.frame.topLeft(), stencil::support::topLeftAt(drop, seen.frame.size(), screenOf(win)));
    ModalSeen after;
    catchModal(after, stencil::support::uiTimings().doubleClickMs + 400);
    QTest::qWait(stencil::support::uiTimings().doubleClickMs + 300);
    QVERIFY2(!after.seen && !win.pop.active, "the click or the double-click still opened something");
  }

  // A popover up when an icon is dragged off unwinds, and the full window opens on the drop.
  void aPopoverDraggedOffBecomesTheFullWindow() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QWidget* icon = win.buttonForAction(win.acts.settings);
    QVERIFY(icon);
    const QPoint drop = win.scroll->mapToGlobal(QPoint(200, 180));
    ModalSeen seen;
    bool wasUp = false;
    QTimer::singleShot(40, &win, [&] {
      wasUp = win.pop.active;
      catchModal(seen);
      dragIcon(icon, drop, drop);
    });
    win.pop.anchor = icon;
    win.acts.settings->trigger();   // the popover's own loop, which the drag above runs inside
    QVERIFY2(wasUp, "the icon's popover never opened");
    QTRY_VERIFY2(seen.seen, "the full window never opened");
    QCOMPARE(seen.frame.topLeft(), stencil::support::topLeftAt(drop, seen.frame.size(), screenOf(win)));
    QVERIFY(!win.pop.active);
  }

  void theChatIconDocksOnABandOrFloatsWhereItDrops() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(!win.chatDock->isVisible());
    QWidget* icon = win.buttonForAction(win.acts.chat);
    QVERIFY(icon);
    liftIcon(icon);
    QWidget* zones = win.overlays.dockZones;
    QVERIFY2(zones && zones->isVisible(), "a chat drag shows the dock bands");
    const QPoint left = zones->mapToGlobal(QPoint(30, zones->height() / 2));
    dropIcon(icon, left);
    QTRY_VERIFY(win.chatDock->isVisible());
    QCOMPARE(win.dockWidgetArea(win.chatDock), Qt::LeftDockWidgetArea);
    QVERIFY(!win.chatDock->isFloating() && win.acts.chat->isChecked() && !zones->isVisible());

    icon = win.buttonForAction(win.acts.chat);   // the docked chat moved the toolbar along
    const QPoint right = zones->mapToGlobal(QPoint(zones->width() - 30, zones->height() / 2));
    dragIcon(icon, right, right);
    QTRY_COMPARE(win.dockWidgetArea(win.chatDock), Qt::RightDockWidgetArea);
    QVERIFY(win.chatDock->isVisible() && !win.chatDock->isFloating());

    icon = win.buttonForAction(win.acts.chat);
    const QPoint mid = win.scroll->mapToGlobal(win.scroll->rect().center());
    dragIcon(icon, mid, mid);
    QTRY_VERIFY(win.chatDock->isFloating() && win.chatDock->isVisible());
    const QRect floated = win.parts.dockChrome.chatFloatRect;
    QCOMPARE(floated.topLeft(), stencil::support::topLeftAt(mid, floated.size(), screenOf(win)));
    QCOMPARE(win.chatDock->geometry(), floated);

    win.acts.chat->setChecked(false);
    QTRY_VERIFY(!win.chatDock->isVisible());
    const QPoint elsewhere = win.scroll->mapToGlobal(QPoint(win.scroll->width() / 3, win.scroll->height() / 2));
    dragIcon(win.buttonForAction(win.acts.chat), elsewhere, elsewhere);
    QTRY_VERIFY(win.chatDock->isVisible() && win.chatDock->isFloating() && win.acts.chat->isChecked());
    QCOMPARE(win.chatDock->geometry().center(), win.parts.dockChrome.chatFloatRect.center());

    const QRect before = win.chatDock->geometry();
    icon = win.buttonForAction(win.acts.chat);
    dragIcon(icon, right, iconCentre(icon));
    QTest::qWait(stencil::support::uiTimings().doubleClickMs + 200);
    QVERIFY2(win.chatDock->isVisible() && win.chatDock->geometry() == before && !zones->isVisible(),
             "released back on its icon the chat stays as it was");

    // A click and then a drag: no compact popover, and the click's toggle never fires after.
    win.acts.chat->setChecked(false);
    QTRY_VERIFY(!win.chatDock->isVisible());
    icon = win.buttonForAction(win.acts.chat);
    iconMouse(icon, QEvent::MouseButtonPress, iconCentre(icon));
    iconMouse(icon, QEvent::MouseButtonRelease, iconCentre(icon));
    iconMouse(icon, QEvent::MouseButtonDblClick, iconCentre(icon));
    iconMouse(icon, QEvent::MouseMove, iconCentre(icon) + QPoint(0, 40));
    dropIcon(icon, mid);
    QTRY_VERIFY(win.chatDock->isVisible() && win.chatDock->isFloating());
    QTest::qWait(stencil::support::uiTimings().doubleClickMs + 200);
    QVERIFY2(win.chatDock->isVisible() && !win.parts.dockChrome.chatCompactPopover && win.acts.chat->isChecked(),
             "a click and then a drag still toggled the chat or opened its popover");
    QCOMPARE(win.chatDock->geometry(), win.parts.dockChrome.chatFloatRect);
  }

  // Lifted past the slop, only the named icons start a drag; Escape ends each one that does.
  void onlyTheNamedIconsDrag() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    QVERIFY(openLoaded(win) && win.canvas->hasImage());
    stencil::core::Line line;
    line.points = {{20, 20}, {80, 60}};
    win.canvas->commitLines({line});
    settleLayout(&win, 150);
    const auto lifts = [](QWidget* icon) {
      liftIcon(icon);
      const bool live = stencil::support::iconDragActive(icon);
      QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
      QApplication::sendEvent(icon->window(), &esc);
      iconMouse(icon, QEvent::MouseButtonRelease, iconCentre(icon) + QPoint(0, 40));
      QApplication::processEvents();
      return live;
    };
    for (QAction* act : {win.acts.chat, win.acts.projects, win.acts.settings, win.acts.crop, win.acts.clearAll, win.acts.rotateLeft,
                         win.acts.rotateRight, win.acts.flipImage, win.acts.zoomIn, win.acts.zoomOut, win.acts.fit}) {
      QWidget* icon = win.buttonForAction(act);
      QVERIFY2(icon && lifts(icon), qPrintable(act->text() + " does not drag"));
    }
    for (QAction* act : {win.acts.undo, win.acts.redo, win.acts.saveImage, win.acts.copyImage, win.acts.copyProject,
                         win.acts.saveProjectFile, win.acts.openProjectFile, win.acts.startDraw,
                         win.acts.fullscreen, win.acts.copyLayout, win.acts.downloadJson, win.acts.clearProject}) {
      QWidget* icon = win.buttonForAction(act);
      if (icon) QVERIFY2(!lifts(icon), qPrintable(act->text() + " drags, and should not"));
    }
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.toolbarDrags.gui.moc"
