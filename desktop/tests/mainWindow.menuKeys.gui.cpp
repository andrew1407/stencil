// MainWindow GUI e2e — Driving the context menu and its flyouts from the keyboard alone.
// Shared ground (helpers, the loaded window, the motion pins) is in mainWindow.gui.hpp.
#include "mainWindow.gui.hpp"

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
      noFlash = !stencil::support::dustMotionOk() || !layoutMenu->isVisible() ||
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
    QVERIFY2(!stencil::support::dustMotionOk() || leftDusted, "Left closed the flyout with no dust flight");
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

  // The Assistant flyout's own version of the rule above: the first → reveals the chat,
  // the second → lands in its text box (user decision — the chat's input, not its
  // first button), so a reply can be typed without touching the mouse.
  void ctxAssistantFlyoutSecondRightFocusesItsInput() {
    MainWindow win(nullptr, false);
    win.settings_.llmProvider = "ollama";
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    const QPoint at = win.mapToGlobal(QPoint(500, 400));
    QCursor::setPos(at);
    QTest::qWait(20);   // the pointer lands before the menu asks where it is
    bool opened = false, revealedOnly = false, entered = false;
    QTimer::singleShot(0, [&] {
      QMenu* root = nullptr;
      for (int i = 0; i < 200 && !root; ++i) {
        root = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!root) QTest::qWait(10);
      }
      if (!root) return;
      settle([&] { return root->windowOpacity() >= 1.0; }, 400);   // the reveal, when one plays
      QAction* assistAct = nullptr;
      for (QAction* a : root->actions()) if (a->text() == "Assistant") assistAct = a;
      if (!assistAct || !assistAct->menu()) { root->close(); return; }
      root->setActiveAction(assistAct);
      QTest::keyClick(root, Qt::Key_Right);
      QMenu* chat = assistAct->menu();
      settle([&] { return chat->isVisible(); }, 1000);
      opened = chat->isVisible();
      QTest::qWait(80);
      revealedOnly = QApplication::focusWidget() != win.chatMenuInput_;
      if (QWidget* p = QApplication::activePopupWidget()) QTest::keyClick(p, Qt::Key_Right);
      QTest::qWait(30);
      entered = QApplication::focusWidget() == win.chatMenuInput_;
      root->close();
    });
    win.showContextMenu(at);
    QVERIFY2(opened, "Right on the Assistant row did not open the chat flyout");
    QVERIFY2(revealedOnly, "the first Right already put the caret in the chat input");
    QVERIFY2(entered, "the second Right did not focus the chat input");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }


  // Radio-style flyouts pick as the keys move (browser parity: arrowing a radio group
  // applies the option at once, menu still open). Image Filter hosts real QRadioButtons:
  // the second → lands on the checked one, ↓/↑ move to the neighbour AND pick it. Style's
  // Solid / Dashed / Dotted are exclusive checkable rows: walking onto one applies it.
  void ctxRadioFlyoutsPickAsTheKeysMove() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    const QPoint at = win.mapToGlobal(QPoint(500, 400));
    QCursor::setPos(at);
    QTest::qWait(20);   // the pointer lands before the menu asks where it is
    const auto checkedFilter = [&win] {
      for (QAbstractButton* b : win.filterButtons_->buttons())
        if (b->isChecked()) return b->property("filterValue").toString();
      return QString();
    };
    bool filterOpened = false, landedOnChecked = false, downPicked = false, upPicked = false,
         stayedOpen = false, styleOpened = false, styleApplied = false, styleStayedOpen = false,
         kbIconMotion = false, rootRouteEntered = false, rootRoutePicked = false, rootRouteFolded = false;
    QString afterDown, afterUp, afterRootDown;
    QTimer::singleShot(0, [&] {
      QMenu* root = nullptr;
      for (int i = 0; i < 200 && !root; ++i) {
        root = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!root) QTest::qWait(10);
      }
      if (!root) return;
      settle([&] { return root->windowOpacity() >= 1.0; }, 400);   // the reveal, when one plays
      auto rowNamed = [&](const QString& title) -> QAction* {
        for (QAction* a : root->actions()) if (a->text().startsWith(title)) return a;
        return nullptr;
      };
      // Walk down from the top of the root to a row (the keys go to the active popup).
      auto walkTo = [&](QAction* act) {
        return walkMenu(root, Qt::Key_Down, [&] { return root->activeAction() == act; });
      };
      QAction* filterAct = rowNamed("Image Filter");
      if (!filterAct || !filterAct->menu() || !walkTo(filterAct)) { root->close(); return; }
      // Landing on a row with the keys is a hover: its icon motion runs (iconMotion.hpp).
      QTest::qWait(60);
      kbIconMotion = stencil::support::motionReduced()
                     || stencil::gui::icm::runnerOfAction(filterAct) != nullptr;
      QTest::keyClick(root, Qt::Key_Right);
      QMenu* filter = filterAct->menu();
      settle([&] { return filter->isVisible(); }, 1000);
      filterOpened = filter->isVisible();
      QTest::qWait(600);   // past the flyout guard's 220/480ms grace — a real hold, not a settle
      if (QWidget* p = QApplication::activePopupWidget()) QTest::keyClick(p, Qt::Key_Right);   // enter
      QTest::qWait(50);
      auto* focused = qobject_cast<QRadioButton*>(QApplication::focusWidget());
      landedOnChecked = focused && focused->isChecked() && checkedFilter() == "none";
      QTest::keyClick(QApplication::focusWidget(), Qt::Key_Down);
      QTest::qWait(80);
      afterDown = checkedFilter();
      downPicked = afterDown == "bw" && qobject_cast<QRadioButton*>(QApplication::focusWidget())
                   && qobject_cast<QRadioButton*>(QApplication::focusWidget())->isChecked();
      QTest::keyClick(QApplication::focusWidget(), Qt::Key_Down);
      QTest::qWait(80);
      QTest::keyClick(QApplication::focusWidget(), Qt::Key_Up);
      QTest::qWait(80);
      afterUp = checkedFilter();
      upPicked = afterUp == "bw";
      stayedOpen = filter->isVisible() && root->isVisible();
      QTest::keyClick(QApplication::focusWidget(), Qt::Key_Left);
      settle([&] { return !(filter->isVisible()); }, 1000);

      // The other route: a flyout opened the way a HOVER opens it leaves the keyboard
      // with the root. Its keys must still reach the focused radio (stayOpenMenu.cpp
      // forwards them), so → enters and ↓ picks exactly as above.
      root->setActiveAction(filterAct);
      settle([&] { return filter->isVisible(); }, 1000);
      QTest::qWait(600);   // past the flyout guard's 220/480ms grace — a real hold, not a settle
      QTest::keyClick(root, Qt::Key_Right);
      QTest::qWait(50);
      rootRouteEntered = qobject_cast<QRadioButton*>(QApplication::focusWidget()) != nullptr;
      QTest::keyClick(root, Qt::Key_Down);
      QTest::qWait(80);
      afterRootDown = checkedFilter();
      rootRoutePicked = afterRootDown == "sepia";
      QTest::keyClick(root, Qt::Key_Left);
      settle([&] { return !(filter->isVisible()); }, 1000);
      rootRouteFolded = !filter->isVisible() && root->isVisible();

      // Style: reveal it, enter it (the point-size spinner), Tab past both spinners
      // onto its first plain row, then walk the rows — landing on Dashed applies it.
      // Keys go where the platform sends them: the popup's focus widget if it has one.
      auto keyTo = [](Qt::Key k) {
        QWidget* popup = QApplication::activePopupWidget();
        QWidget* receiver = popup && popup->focusWidget() ? popup->focusWidget() : popup;
        QTest::keyClick(receiver, k);
      };
      QAction* styleAct = rowNamed("Style");
      if (!styleAct || !styleAct->menu()) { root->close(); return; }
      walkMenu(root, Qt::Key_Up, [&] { return root->activeAction() == styleAct; });
      QTest::keyClick(root, Qt::Key_Right);
      QMenu* style = styleAct->menu();
      settle([&] { return style->isVisible(); }, 1000);
      styleOpened = style->isVisible();
      QTest::qWait(600);   // past the flyout guard's 220/480ms grace — a real hold, not a settle
      keyTo(Qt::Key_Right);   // enter: the point-size spinner
      QTest::qWait(40);
      keyTo(Qt::Key_Tab);     // thickness
      QTest::qWait(40);
      keyTo(Qt::Key_Tab);     // off the last control → the first plain row (Solid)
      QTest::qWait(40);
      for (int i = 0; i < 6 && style->activeAction() != win.actStyleDashed_; ++i) {
        keyTo(Qt::Key_Down);
        QTest::qWait(30);
      }
      styleApplied = style->activeAction() == win.actStyleDashed_ && win.actStyleDashed_->isChecked()
                     && win.settings_.defaultStyle == "dashed";
      styleStayedOpen = style->isVisible() && root->isVisible();
      root->close();
    });
    win.showContextMenu(at);
    QVERIFY2(filterOpened, "Right on the Image Filter row did not open its flyout");
    QVERIFY2(landedOnChecked, "the second Right did not land on the checked radio (None)");
    QVERIFY2(downPicked, qPrintable("Down did not pick the next filter — checked: " + afterDown));
    QVERIFY2(upPicked, qPrintable("Up did not pick the previous filter — checked: " + afterUp));
    QVERIFY2(stayedOpen, "picking a filter with the arrows closed the menu");
    QVERIFY2(kbIconMotion, "landing on a row with the keys did not run its icon motion");
    QVERIFY2(rootRouteEntered, "root-held keys: Right did not focus a radio in the hover-opened flyout");
    QVERIFY2(rootRoutePicked, qPrintable("root-held keys: Down did not pick the next filter — checked: " + afterRootDown));
    QVERIFY2(rootRouteFolded, "root-held keys: Left did not fold the flyout");
    QVERIFY2(styleOpened, "Right on the Style row did not open its flyout");
    QVERIFY2(styleApplied, "walking onto Dashed did not apply the dashed style");
    QVERIFY2(styleStayedOpen, "applying a style with the arrows closed the menu");
    win.applyImageFilter("none");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }


  // The Custom Tint pick shows the "Tint Color…" row at once, and moving off it hides
  // the row again — while the flyout is open (browser parity: .ctx-tint-visible follows
  // the radio change), not only on the next open.
  void ctxCustomTintRowFollowsTheFilterPick() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    const QPoint at = win.mapToGlobal(QPoint(500, 400));
    QCursor::setPos(at);
    QTest::qWait(20);   // the pointer lands before the menu asks where it is
    bool opened = false, hiddenAtStart = false, shownOnCustom = false, rowLaidOut = false, hiddenAgain = false;
    QTimer::singleShot(0, [&] {
      QMenu* root = nullptr;
      for (int i = 0; i < 200 && !root; ++i) {
        root = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!root) QTest::qWait(10);
      }
      if (!root) return;
      settle([&] { return root->windowOpacity() >= 1.0; }, 400);   // the reveal, when one plays
      QAction* filterAct = nullptr;
      for (QAction* a : root->actions()) if (a->text().startsWith("Image Filter")) filterAct = a;
      if (!filterAct || !filterAct->menu()) { root->close(); return; }
      walkMenu(root, Qt::Key_Down, [&] { return root->activeAction() == filterAct; });
      QTest::keyClick(root, Qt::Key_Right);
      QMenu* filter = filterAct->menu();
      settle([&] { return filter->isVisible(); }, 1000);
      opened = filter->isVisible();
      QTest::qWait(600);   // past the flyout guard's 220/480ms grace — a real hold, not a settle
      hiddenAtStart = !win.tintColorAction_->isVisible();
      if (QWidget* p = QApplication::activePopupWidget()) QTest::keyClick(p, Qt::Key_Right);   // enter: None
      QTest::qWait(50);
      for (int i = 0; i < 5; ++i) { QTest::keyClick(QApplication::focusWidget(), Qt::Key_Down); QTest::qWait(60); }
      QTest::qWait(100);
      shownOnCustom = win.settings_.imageFilter == "custom" && win.tintColorAction_->isVisible();
      rowLaidOut = filter->actionGeometry(win.tintColorAction_).isValid()
                   && filter->height() >= filter->actionGeometry(win.tintColorAction_).bottom();
      QTest::keyClick(QApplication::focusWidget(), Qt::Key_Up);
      QTest::qWait(100);
      hiddenAgain = win.settings_.imageFilter == "contour" && !win.tintColorAction_->isVisible();
      root->close();
    });
    win.showContextMenu(at);
    QVERIFY2(opened, "Right on the Image Filter row did not open its flyout");
    QVERIFY2(hiddenAtStart, "the tint row was showing with no custom filter active");
    QVERIFY2(shownOnCustom, "picking Custom Tint did not show the tint row");
    QVERIFY2(rowLaidOut, "the tint row is visible but the flyout did not make room for it");
    QVERIFY2(hiddenAgain, "moving off Custom Tint did not hide the tint row");
    win.applyImageFilter("none");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    // Against the screen's bottom edge: the flyout that grows for the tint row must be
    // re-placed to stay on screen, or the new row lands below it, never seen.
    const QRect avail = win.screen()->availableGeometry();
    win.move(avail.left() + 40, avail.bottom() - win.height() - 10);
    QTest::qWait(200);
    const QPoint low = win.mapToGlobal(QPoint(500, win.height() - 60));
    QCursor::setPos(low);
    QTest::qWait(20);   // the pointer lands before the menu asks where it is
    bool lowOpened = false, onScreen = false, rowOnScreen = false;
    QTimer::singleShot(0, [&] {
      QMenu* root = nullptr;
      for (int i = 0; i < 200 && !root; ++i) {
        root = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!root) QTest::qWait(10);
      }
      if (!root) return;
      settle([&] { return root->windowOpacity() >= 1.0; }, 400);   // the reveal, when one plays
      QAction* filterAct = nullptr;
      for (QAction* a : root->actions()) if (a->text().startsWith("Image Filter")) filterAct = a;
      if (!filterAct || !filterAct->menu()) { root->close(); return; }
      root->setActiveAction(filterAct);   // hover-style open
      QMenu* filter = filterAct->menu();
      settle([&] { return filter->isVisible(); }, 1000);
      lowOpened = filter->isVisible();
      QTest::qWait(600);   // past the flyout guard's 220/480ms grace — a real hold, not a settle
      QAbstractButton* custom = nullptr;
      for (QAbstractButton* b : win.filterButtons_->buttons()) if (b->property("filterValue") == "custom") custom = b;
      QTest::mouseClick(custom, Qt::LeftButton, Qt::NoModifier, custom->rect().center());
      QTest::qWait(300);
      onScreen = avail.contains(filter->geometry());
      const QRect row = filter->actionGeometry(win.tintColorAction_);
      rowOnScreen = row.isValid() && avail.contains(QRect(filter->mapToGlobal(row.topLeft()), row.size()));
      root->close();
    });
    win.showContextMenu(low);
    QVERIFY2(lowOpened, "the low flyout did not open");
    QVERIFY2(onScreen, "the flyout grew off the bottom of the screen");
    QVERIFY2(rowOnScreen, "the tint row landed off screen");
    win.applyImageFilter("none");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }


  // A flyout the first → only revealed still belongs to the parent's walk: ↓ moves the
  // ROOT highlight and folds the flyout (browser parity), and only after the second →
  // do ↑/↓ work inside it. Before, Qt walked the revealed flyout's rows, which for the
  // radio flyout read as "the keys only move the radio focus".
  void ctxRevealedFlyoutArrowsWalkTheParent() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    const QPoint at = win.mapToGlobal(QPoint(500, 400));
    QCursor::setPos(at);
    QTest::qWait(20);   // the pointer lands before the menu asks where it is
    bool revealed = false, downFolded = false, upBack = false, reRevealed = false, enteredPick = false;
    QString rootAfterDown;
    QTimer::singleShot(0, [&] {
      QMenu* root = nullptr;
      for (int i = 0; i < 200 && !root; ++i) {
        root = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!root) QTest::qWait(10);
      }
      if (!root) return;
      settle([&] { return root->windowOpacity() >= 1.0; }, 400);   // the reveal, when one plays
      QAction* filterAct = nullptr;
      QAction* transformAct = nullptr;
      for (QAction* a : root->actions()) {
        if (a->text().startsWith("Image Filter")) filterAct = a;
        if (a->text().startsWith("Transformation")) transformAct = a;
      }
      if (!filterAct || !filterAct->menu() || !transformAct) { root->close(); return; }
      // keyClick derefs its receiver (QTEST_ASSERT is compiled out in Release), so a chain
      // that has already closed segfaults the whole binary and takes every later case with
      // it. Bail out instead and let the QVERIFY2s below report the miss.
      auto toPopup = [](Qt::Key k) {
        QWidget* p = QApplication::activePopupWidget();
        if (p) QTest::keyClick(p, k);
        return p;
      };
      walkMenu(root, Qt::Key_Down, [&] { return root->activeAction() == filterAct; });
      QTest::keyClick(root, Qt::Key_Right);
      QMenu* filter = filterAct->menu();
      settle([&] { return filter->isVisible(); }, 1000);
      revealed = filter->isVisible();
      QTest::qWait(600);   // past the flyout guard's 220/480ms grace — a real hold, not a settle
      // ↓ while only revealed: the parent walks on (to Transformation) and the flyout folds.
      if (!toPopup(Qt::Key_Down)) { root->close(); return; }
      settle([&] { return !(filter->isVisible()); }, 1000);
      rootAfterDown = root->activeAction() ? root->activeAction()->text() : QString();
      downFolded = !filter->isVisible() && root->activeAction() == transformAct;
      if (!toPopup(Qt::Key_Up)) { root->close(); return; }
      QTest::qWait(60);
      upBack = root->activeAction() == filterAct && !filter->isVisible();
      // → reveals again, a second → enters, and now ↓ picks inside.
      if (!toPopup(Qt::Key_Right)) { root->close(); return; }
      settle([&] { return filter->isVisible(); }, 1000);
      reRevealed = filter->isVisible();
      QTest::qWait(600);   // past the flyout guard's 220/480ms grace — a real hold, not a settle
      QWidget* popup = toPopup(Qt::Key_Right);
      if (!popup) { root->close(); return; }
      QTest::qWait(50);
      // Keys go where the platform sends them: the popup's focus widget (the radio).
      popup = QApplication::activePopupWidget();
      if (!popup) { root->close(); return; }
      QWidget* focused = popup->focusWidget();
      QTest::keyClick(focused ? focused : popup, Qt::Key_Down);
      QTest::qWait(80);
      enteredPick = win.settings_.imageFilter == "bw" && filter->isVisible();
      root->close();
    });
    win.showContextMenu(at);
    QVERIFY2(revealed, "Right on the Image Filter row did not reveal its flyout");
    QVERIFY2(downFolded, qPrintable("Down on a revealed flyout did not walk the parent on and fold it — root row: " + rootAfterDown));
    QVERIFY2(upBack, "Up did not walk the parent back to Image Filter");
    QVERIFY2(reRevealed, "Right did not reveal the flyout again");
    QVERIFY2(enteredPick, "after the second Right, Down did not pick inside the flyout");
    win.applyImageFilter("none");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "mainWindow.menuKeys.gui.moc"
