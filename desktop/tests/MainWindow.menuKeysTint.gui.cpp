// MainWindow GUI e2e — The custom-tint row following its filter pick, and arrows walking a revealed flyout's parent.
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
#include "MainWindow.menuKeysTint.gui.moc"
