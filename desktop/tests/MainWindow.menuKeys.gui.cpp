// MainWindow GUI e2e — Keyboard navigation into a context submenu, and Tab walking a flyout's controls.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // A repeat of the same message (e.g. pan/zoom's debounced "Saved") landing while the LAST
  // one is still mid-exit used to coexist with it instead of coalescing — liveToasts() only
  // coalesces into a STANDING toast, so the fresh arrival's opaque label buried the leaving
  // one's still-playing dust. Only one "toast" label should ever exist for a given message.

  // REGRESSION: Right on a submenu row opened the flyout and it vanished ~220ms later
  // (or never got past its reveal). Opening from the keyboard makes Qt re-emit hovered()
  // on the parent for a row the pointer never touched, and SubmenuCloseGuard
  // (menuReveal.cpp) armed its close on that. Only pointer-made hovers may arm it.
  void ctxSubmenuOpenedByKeyboardStaysOpen() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    const QPoint at = win.mapToGlobal(QPoint(500, 400));
    QCursor::setPos(at);   // the pointer rests where the menu opens, as after a right-click
    QTest::qWait(20);   // the pointer lands before the menu asks where it is
    bool opened = false, stillOpen = false, walkedInside = false, leftClosed = false,
         reopened = false, closedByPointer = false, reachedPoints = false, enterClosed = false,
         pointsBefore = false, noFlash = false, enteredRow = false, leftDusted = false;
    QTimer::singleShot(0, [&] {
      QMenu* root = nullptr;
      for (int i = 0; i < 200 && !root; ++i) {
        root = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!root) QTest::qWait(10);
      }
      if (!root) return;
      settle([&] { return root->windowOpacity() >= 1.0; }, 400);   // the reveal, when one plays
      QAction* layoutAct = nullptr;
      for (QAction* a : root->actions()) if (a->text() == "Image / Layout") layoutAct = a;
      if (!layoutAct || !layoutAct->menu()) { root->close(); return; }
      // Walk down to the row with the keyboard, like a user would, then open it.
      walkMenu(root, Qt::Key_Down, [&] { return root->activeAction() == layoutAct; }, 12, 30);
      QTest::keyClick(root, Qt::Key_Right);
      QMenu* layoutMenu = layoutAct->menu();
      // Veiled from its very first frame when the dust reveal is on: a flyout that
      // paints solid for a frame and THEN plays its reveal reads as a flash.
      noFlash = !stencil::support::isDustMotionOk() || !layoutMenu->isVisible() ||
                layoutMenu->windowOpacity() < 1.0;
      settle([&] { return layoutMenu->isVisible(); }, 1000);
      opened = layoutMenu->isVisible();
      QTest::qWait(900);   // well past the guard's 220/480ms grace
      stillOpen = layoutMenu->isVisible();
      // The rest of the walk: a second Right lands on the flyout's first REAL row (not
      // its "IMAGE" title), Down moves on past the title rows, Left closes it back
      // onto the parent row with the root still up, Right reopens it.
      if (QWidget* p = QApplication::activePopupWidget()) QTest::keyClick(p, Qt::Key_Right);
      QTest::qWait(30);
      enteredRow = layoutMenu->activeAction() && layoutMenu->activeAction()->text().startsWith("Copy Image");
      QTest::keyClick(layoutMenu, Qt::Key_Down);
      QTest::qWait(30);
      walkedInside = layoutMenu->activeAction() == win.actPasteImage_;
      // ← folds it with the same dust every other close plays (Qt hides the popup
      // before aboutToHide fires, which used to leave this close with no flight).
      const auto dustSeen = [&win] {
        for (QWidget* w : win.findChildren<QWidget*>(
                 QString::fromLatin1(stencil::gui::DisintegrateOverlay::OBJECT_NAME)))
          if (static_cast<stencil::gui::DisintegrateOverlay*>(w)->surfacePicture().isValid()) return true;
        return false;
      };
      QTest::keyClick(layoutMenu, Qt::Key_Left);
      for (int i = 0; i < 100 && layoutMenu->isVisible(); ++i) { QTest::qWait(10); if (dustSeen()) leftDusted = true; }
      if (dustSeen()) leftDusted = true;
      leftClosed = !layoutMenu->isVisible() && root->isVisible() && root->activeAction() == layoutAct;
      QTest::keyClick(root, Qt::Key_Right);
      settle([&] { return layoutMenu->isVisible(); }, 1000);
      reopened = layoutMenu->isVisible();
      // …while a real pointer move onto another row still closes it (the guard's job).
      QAction* plainRow = nullptr;
      for (QAction* a : root->actions()) {
        if (a->isSeparator() || a->menu() || !a->isEnabled()) continue;
        plainRow = a; break;
      }
      if (plainRow) {
        const QPoint from = root->actionGeometry(layoutAct).center();
        const QPoint to = root->actionGeometry(plainRow).center();
        for (int i = 1; i <= 8; ++i) {
          const QPoint p = from + (to - from) * i / 8;
          QMouseEvent e(QEvent::MouseMove, QPointF(p), QPointF(root->mapToGlobal(p)),
                        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
          QApplication::sendEvent(root, &e);
          QTest::qWait(15);
        }
        settle([&] { return !(layoutMenu->isVisible()); }, 800);
        closedByPointer = !layoutMenu->isVisible();
      }
      // Enter picks a row: walk the root to Show Points and toggle it, which also
      // closes the menu (a picked action, not a hosted checkbox row).
      pointsBefore = win.actShowPoints_->isChecked();
      walkMenu(root, Qt::Key_Down, [&] { return root->activeAction() == win.actShowPoints_; }, 24);
      reachedPoints = root->activeAction() == win.actShowPoints_;
      QTest::keyClick(root, Qt::Key_Return);
      settle([&] { return !(root->isVisible()); }, 1000);
      enterClosed = !root->isVisible();
      if (root->isVisible()) root->close();
    });
    win.showContextMenu(at);
    QVERIFY2(opened, "Right on the Image / Layout row did not open its submenu");
    QVERIFY2(stillOpen, "the keyboard-opened submenu closed on its own");
    QVERIFY2(noFlash, "the flyout painted solid before its reveal played");
    QVERIFY2(enteredRow, "the second Right did not land on Copy Image (the first real row)");
    QVERIFY2(walkedInside, "Down from Copy Image did not reach Paste Image");
    QVERIFY2(leftClosed, "Left did not close the flyout back onto its parent row");
    QVERIFY2(!stencil::support::isDustMotionOk() || leftDusted, "Left closed the flyout with no dust flight");
    QVERIFY2(reopened, "Right did not reopen the flyout");
    QVERIFY2(closedByPointer, "hovering the pointer onto another row no longer closes it");
    QVERIFY2(reachedPoints, "the keyboard walk never reached Show Points");
    QVERIFY2(enterClosed, "Return did not pick the row and close the menu");
    QCOMPARE(win.actShowPoints_->isChecked(), !pointsBefore);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }

  // Tab inside a flyout that hosts real controls (Style's spinners here) walks those
  // controls, wrapping, instead of QMenu's default "Tab is ↓" that never reached them
  //. The keyboard-opened submenu is the active popup, so keys go to it.
  void ctxFlyoutTabWalksItsControls() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    const QPoint at = win.mapToGlobal(QPoint(500, 400));
    QCursor::setPos(at);
    QTest::qWait(20);   // the pointer lands before the menu asks where it is
    bool opened = false, revealedOnly = false, foldedBack = false, wrappedToLastRow = false;
    QWidget *first = nullptr, *second = nullptr, *backAgain = nullptr, *wrapped = nullptr;
    QTimer::singleShot(0, [&] {
      QMenu* root = nullptr;
      for (int i = 0; i < 200 && !root; ++i) {
        root = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!root) QTest::qWait(10);
      }
      if (!root) return;
      settle([&] { return root->windowOpacity() >= 1.0; }, 400);   // the reveal, when one plays
      QAction* styleAct = nullptr;
      for (QAction* a : root->actions()) if (a->text() == "Style") styleAct = a;
      if (!styleAct || !styleAct->menu()) { root->close(); return; }
      root->setActiveAction(styleAct);
      QTest::keyClick(root, Qt::Key_Right);
      QMenu* style = styleAct->menu();
      settle([&] { return style->isVisible(); }, 1000);
      opened = style->isVisible();
      QTest::qWait(60);
      // The first → only revealed it: no control has focus yet, so ← can fold it back.
      QWidget* popup = QApplication::activePopupWidget();
      revealedOnly = QApplication::focusWidget() != win.pointSpin_ && QApplication::focusWidget() != win.thickSpin_;
      QTest::keyClick(popup, Qt::Key_Left);
      settle([&] { return !(style->isVisible()); }, 1000);
      foldedBack = !style->isVisible() && root->isVisible();
      QTest::keyClick(root, Qt::Key_Right);
      settle([&] { return style->isVisible(); }, 1000);
      QTest::qWait(60);
      // The second → enters it, onto the first control; Tab walks on from there.
      popup = QApplication::activePopupWidget();
      QTest::keyClick(popup, Qt::Key_Right);
      QTest::qWait(30);
      first = QApplication::focusWidget();
      QTest::keyClick(popup, Qt::Key_Tab);
      QTest::qWait(30);
      second = QApplication::focusWidget();
      QTest::keyClick(popup, Qt::Key_Backtab);
      QTest::qWait(30);
      backAgain = QApplication::focusWidget();
      QTest::keyClick(popup, Qt::Key_Backtab);   // …and off the first control onto the LAST row
      QTest::qWait(30);
      wrapped = QApplication::focusWidget();
      wrappedToLastRow = style->activeAction() == win.actStyleDotted_;
      root->close();
    });
    win.showContextMenu(at);
    QVERIFY2(opened, "Right on the Style row did not open its flyout");
    QVERIFY2(revealedOnly, "the first Right already moved focus into a control");
    QVERIFY2(foldedBack, "Left after the first Right did not fold the flyout back");
    QCOMPARE(first, static_cast<QWidget*>(win.pointSpin_));
    QCOMPARE(second, static_cast<QWidget*>(win.thickSpin_));
    QCOMPARE(backAgain, static_cast<QWidget*>(win.pointSpin_));
    // Shift+Tab off the first control bridges onto the flyout's LAST plain row (Dotted):
    // the keys go back to the menu (no control focused) and ↑/↓ walk the rows from there.
    QVERIFY2(!wrapped || (wrapped != win.pointSpin_ && wrapped != win.thickSpin_),
             "Shift+Tab off the first control left a spinner focused");
    QVERIFY2(wrappedToLastRow, "Shift+Tab off the first control did not land on the last row");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.menuKeys.gui.moc"
