// MainWindow GUI e2e — Opening the context menu: the submenu hover/dust probe and the Shift+F10 route.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindowMenu.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // A context submenu hover-opens and SubmenuCloseGuard closes it on a hover-away; on a real display it
  // also dusts on every open. isDustMotionOk() refuses offscreen, so that half reports SKIPPED.
  void ctxSubmenuDustReplayProbe() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());

    const auto moveTo = [](QMenu* m, const QPoint& p) {
      QMouseEvent e(QEvent::MouseMove, QPointF(p), QPointF(m->mapToGlobal(p)),
                    Qt::NoButton, Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(m, &e);
    };
    const auto hoverPath = [&](QMenu* m, const QPoint& from, const QPoint& to) {
      for (int i = 1; i <= 8; ++i) { moveTo(m, from + (to - from) * i / 8); QTest::qWait(15); }
    };
    const auto dustSeen = [&win] {
      for (QWidget* w : win.findChildren<QWidget*>(
               QString::fromLatin1(stencil::gui::DisintegrateOverlay::OBJECT_NAME))) {
        auto* fx = static_cast<stencil::gui::DisintegrateOverlay*>(w);
        if (fx->surfacePicture().isValid()) return true;
      }
      return false;
    };

    bool dustOnFirstOpen = false, dustOnClose = false, closed = false, dustOnSecondOpen = false;
    QTimer::singleShot(0, [&] {
      QMenu* menu = nullptr;
      for (int i = 0; i < 200 && !menu; ++i) {
        menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) QTest::qWait(10);
      }
      if (!menu) return;
      QAction* parent = nullptr;
      for (QAction* a : menu->actions())
        if (a->text().startsWith("Image / Layout")) parent = a;
      if (!parent || !parent->menu()) return;
      QAction* plainRow = nullptr;
      for (QAction* a : menu->actions()) {
        if (a->isSeparator() || a->menu() || !a->isEnabled()) continue;
        plainRow = a; break;
      }
      if (!plainRow) return;
      const QPoint plainCenter = menu->actionGeometry(plainRow).center();
      const QPoint parentCenter = menu->actionGeometry(parent).center();
      QMenu* sub = parent->menu();

      // Open #1.
      for (int attempt = 0; attempt < 4 && !sub->isVisible(); ++attempt) {
        moveTo(menu, plainCenter); QTest::qWait(30);
        moveTo(menu, parentCenter);
        settle([&] { return sub->isVisible(); }, 400);
      }
      if (!sub->isVisible()) { menu->close(); return; }
      dustOnFirstOpen = dustSeen();

      // Hover away — our own SubmenuCloseGuard should hide it AND dust it.
      hoverPath(menu, parentCenter, plainCenter);
      for (int i = 0; i < 60 && sub->isVisible(); ++i) { QTest::qWait(10); if (dustSeen()) dustOnClose = true; }
      // The flight is spawned by the hide itself (menuReveal.cpp dustMenuOut off
      // aboutToHide), so it is only there to see once the popup has gone.
      if (dustSeen()) dustOnClose = true;
      closed = !sub->isVisible();

      // Open #2 — the SAME QMenu instance, reopened.
      hoverPath(menu, plainCenter, parentCenter);
      settle([&] { return sub->isVisible(); }, 600);
      dustOnSecondOpen = dustSeen();

      menu->close();
    });
    win.showContextMenu(win.mapToGlobal(QPoint(400, 300)));

    // Correctness first, and it holds on every platform: our guard really does close a
    // hovered-away submenu (Qt itself leaves it up).
    QVERIFY2(closed, "the submenu never closed");
    if (!stencil::support::isDustMotionOk())
      QSKIP("dust is gated off on the offscreen platform (isDustMotionOk) — "
            "run this binary on a real display to exercise the flights");
    QVERIFY2(dustOnFirstOpen, "no dust on the first open");
    QVERIFY2(dustOnClose, "no dust while our own guard closed the submenu");
    QVERIFY2(dustOnSecondOpen, "no dust replayed on the second open of the same submenu");
  }
  // Shift+F10 (shared hotkeysConfig contextMenu) opens the canvas context menu from the keyboard: under
  // the pointer while it rests over the viewport, else at the viewport's centre, as the browser does.
  void contextMenuOpensOnShiftF10() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QVERIFY(win.actContextMenu);
    QCOMPARE(win.actContextMenu->shortcut(), QKeySequence("Shift+F10"));
    if (QWidget* fw = QApplication::focusWidget()) fw->clearFocus();
    win.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&win));   // a WindowShortcut needs the active window
    QWidget* vp = win.scroll->viewport();
    const QRect vpGlobal(vp->mapToGlobal(QPoint(0, 0)), vp->size());
    // The menu exec()s: a poll (armed BEFORE the press — the platform key path flushes
    // pending events, so a one-shot would fire too early) records where it opened and closes it.
    const auto armCloser = [&win, vp](bool& opened, QPoint& at, QRect& vpAt) {
      auto* poll = new QTimer(&win);
      poll->setInterval(10);
      int ticks = 0;
      QObject::connect(poll, &QTimer::timeout, &win, [poll, vp, &opened, &at, &vpAt, ticks]() mutable {
        if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
          opened = true;
          at = menu->pos();
          vpAt = QRect(vp->mapToGlobal(QPoint(0, 0)), vp->size());
          menu->close();
          poll->stop();
          poll->deleteLater();
        } else if (++ticks > 300) {
          poll->stop();
          poll->deleteLater();
        }
      });
      poll->start();
    };
    // Pointer resting on the canvas: the menu grows from right there — via the chord
    // itself, through the platform window so the press walks the real shortcut map.
    const QPoint onCanvas = vpGlobal.topLeft() + QPoint(40, 40);
    QCursor::setPos(onCanvas);
    QPoint at1(-1, -1);
    QRect vpAt1;
    bool opened1 = false;
    armCloser(opened1, at1, vpAt1);
    QTest::keyClick(win.windowHandle(), Qt::Key_F10, Qt::ShiftModifier);
    QTRY_VERIFY2_WITH_TIMEOUT(opened1, "Shift+F10 did not open the canvas context menu", 4000);
    // x is the pointer's; y may be pulled up to keep the menu on the (short) offscreen screen.
    QVERIFY2(vpAt1.contains(onCanvas), "the pointer was not over the viewport after all");
    QCOMPARE(at1.x(), onCanvas.x());
    QVERIFY(at1.y() <= onCanvas.y());
    QTRY_VERIFY(!QApplication::activePopupWidget());
    // Pointer off the canvas (on the toolbar): the menu lands at the viewport's centre.
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() - 8, 8)));
    QPoint at2(-1, -1);
    QRect vpAt2;
    bool opened2 = false;
    armCloser(opened2, at2, vpAt2);
    win.actContextMenu->trigger();
    QTRY_VERIFY2_WITH_TIMEOUT(opened2, "the context-menu action did not open the menu", 4000);
    // Against the viewport as it was AT THAT INSTANT: the panel settles into its width after the window
    // opens. x lands on the centre exactly; y may be pulled up to keep the menu on screen.
    QCOMPARE(at2.x(), vpAt2.center().x());
    QVERIFY(at2.y() <= vpAt2.center().y());
    QTRY_VERIFY(!QApplication::activePopupWidget());
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.menusOpening.gui.moc"
