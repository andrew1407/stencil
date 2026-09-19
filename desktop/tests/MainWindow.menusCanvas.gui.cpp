// MainWindow GUI e2e — Opening over the canvas backdrop, and the row a menu-opened dialog flies from.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindowMenu.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The context menu must open anywhere on the canvas SURFACE, not only on the
  // image. The canvas widget is sized to the image, so the backdrop around a
  // zoomed-out image belongs to the scroll area's viewport — which had no menu at
  // all. With NO image there is no menu anywhere: every entry acts on an image
  // (browser contextMenu.js parity).
  void contextMenuOpensOnEmptyCanvasArea() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QWidget* viewport = win.findChild<QScrollArea*>()->viewport();
    QVERIFY(viewport);

    // Right-click a corner of the canvas area — clearly outside any image.
    auto rightClickCorner = [&win, viewport](bool* opened, bool* copyEnabled,
                                             bool* copyFound) {
      QTimer::singleShot(0, [&win, opened, copyEnabled, copyFound] {
        QMenu* menu = nullptr;
        for (int i = 0; i < 200 && !menu; ++i) {
          menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
          if (!menu) QTest::qWait(10);
        }
        if (!menu) return;
        *opened = true;
        // The "Current" copy-image variant action — syncContextActions has just
        // run for this popup. actCopyImage_ is a fixed pointer (not text-matched):
        // its own text no longer starts with "Copy Image" now that it is nested
        // under a "Copy Image ▸" submenu parent (which is a DIFFERENT, always-
        // enabled QAction — the submenu opener, not the image-dependent copy itself).
        *copyFound = win.actCopyImage_ != nullptr;
        *copyEnabled = win.actCopyImage_ && win.actCopyImage_->isEnabled();
        menu->close();
      });
      const QPoint corner(6, 6);
      QTest::mouseClick(viewport, Qt::RightButton, {}, corner);
      QTest::qWait(50);
    };

    // ── no image at all: NO menu — a popup of dead rows is worse than none. Clicked
    // directly (not through rightClickCorner): its poll would spin for two seconds
    // waiting for a menu that never comes, and still be running for the next case.
    QVERIFY(!win.findChild<CanvasWidget*>()->hasImage());
    QTest::mouseClick(viewport, Qt::RightButton, {}, QPoint(6, 6));
    QTest::qWait(50);
    QVERIFY2(!QApplication::activePopupWidget(), "the context menu opened with no image");
    // …and the keyboard route (Shift+F10) goes through the same gate.
    win.showContextMenuFromKeyboard();
    QTest::qWait(30);
    QVERIFY2(!QApplication::activePopupWidget(), "Shift+F10 opened a menu with no image");

    // ── with an image loaded, clicking OUTSIDE it (the backdrop) ──
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    bool openedOutside = false, copyEnabledOutside = false, copyFoundOutside = false;
    rightClickCorner(&openedOutside, &copyEnabledOutside, &copyFoundOutside);
    QVERIFY2(openedOutside, "no context menu on the backdrop around the image");
    QVERIFY2(copyEnabledOutside, "image actions stayed disabled with an image loaded");
    beat();
  }
  // A dialog opened from the MENU BAR must know which row it came out of, so
  // support::revealDialog can grow the window from there when the toolbar icon is hidden.
  void menuOpenedDialogRemembersItsRow() {
    MainWindow win;
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // Any dialog action that lives on both a menu and a toolbar icon.
    QAction* act = nullptr;
    QMenu* owner = nullptr;
    for (QMenu* m : win.menuBar()->findChildren<QMenu*>()) {
      for (QAction* a : m->actions())
        if (win.pop_.dialogActions.contains(a) && a->isEnabled()) { act = a; owner = m; break; }
      if (act) break;
    }
    QVERIFY2(act && owner, "no dialog action on the menu bar to test");

    owner->popup(win.mapToGlobal(QPoint(40, 40)));
    QVERIFY(QTest::qWaitForWindowExposed(owner));
    const QRect row = owner->actionGeometry(act);
    QVERIFY(row.isValid());
    // Hover the row the way a user does, then let the menu close and the action fire.
    QTest::mouseMove(owner, row.center());
    QTest::qWait(30);
    QVERIFY2(win.pop_.menuRowAction == act, "the hovered row was not recorded");
    const QRect rowGlobal(owner->mapToGlobal(row.topLeft()), row.size());
    QCOMPARE(win.pop_.menuRowRect, rowGlobal);
    // Qt hides the menu and THEN activates the action, in the same pass of the event loop.
    // The record has to still be there at that point — this is exactly what reading
    // QApplication::activePopupWidget() inside triggered() got wrong.
    owner->close();
    QVERIFY2(win.pop_.menuRowAction == act, "the row was forgotten before the action fired");
    // The dialog itself blocks in exec(), so drive only the handler that stamps the anchor.
    win.pop_.dialogAnchorRect = (win.pop_.menuRowAction == act) ? win.pop_.menuRowRect : QRect();
    QCOMPARE(win.pop_.dialogAnchorRect, rowGlobal);
    // …and the record does not linger: the next run from an icon/shortcut is not the menu's.
    QTest::qWait(30);
    QVERIFY2(!win.pop_.menuRowAction, "the hovered row outlived its menu");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.menusCanvas.gui.moc"
