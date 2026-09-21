// MainWindow GUI e2e — Fullscreen: the reveal, the zoom it preserves, and that it leaves nothing behind.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Fullscreen edge-hover: the top toolbars and the right points panel reveal WITH AN ANIMATION and
  // do so MONOTONICALLY (no oscillation = no flicker), then hide and fully restore on exit.
  void fullscreenRevealAnimatesSmoothly() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    auto maxBarHeight = [&win] {
      int m = 0;
      for (QToolBar* b : win.findChildren<QToolBar*>())
        if (b->isVisible()) m = std::max(m, b->height());
      return m;
    };
    auto anyBarVisible = [&win] {
      for (QToolBar* b : win.findChildren<QToolBar*>()) if (b->isVisible()) return true;
      return false;
    };
    // Sample a size getter every ~16ms across the ~200ms animation; return the series.
    auto sample = [](auto getter) {
      QList<int> s;
      for (int i = 0; i < 20; ++i) { s.append(getter()); QTest::qWait(16); }
      return s;
    };
    auto hasIntermediate = [](const QList<int>& s, int full) {   // some value strictly inside (0, full)
      for (int v : s) if (v > 2 && v < full - 2) return true;
      return false;
    };
    auto nonDecreasing = [](const QList<int>& s) {
      for (int i = 1; i < s.size(); ++i) if (s[i] < s[i - 1] - 1) return false;   // 1px slack
      return true;
    };
    auto nonIncreasing = [](const QList<int>& s) {
      for (int i = 1; i < s.size(); ++i) if (s[i] > s[i - 1] + 1) return false;
      return true;
    };

    QAction* fs = actionByText(&win, "Enter Fullscreen");
    QVERIFY(fs);
    fs->trigger();                                   // ENTER fullscreen (bars + panel hidden)
    QTest::qWait(120);
    QVERIFY(!anyBarVisible());                        // nothing shown until the cursor hits an edge

    // --- Top toolbars: cursor to the top band → animated slide-in ---
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() / 2, 40)));
    const QList<int> up = sample(maxBarHeight);
    const int full = up.isEmpty() ? 0 : up.last();
    QVERIFY2(full > 10, "toolbars should have revealed to a real height");
    QVERIFY2(hasIntermediate(up, full), "toolbar reveal popped instantly (no intermediate heights)");
    QVERIFY2(nonDecreasing(up), "toolbar reveal height oscillated (flicker)");

    // Cursor well below the keep-zone → animated slide-out.
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() / 2, win.height() - 40)));
    const QList<int> down = sample(maxBarHeight);
    QVERIFY2(nonIncreasing(down), "toolbar hide height oscillated (flicker)");
    QTest::qWait(120);

    // --- Right points panel: cursor to the right edge → animated slide-in reveal ---
    QWidget* panel = nullptr;
    for (QWidget* dw : win.findChildren<QWidget*>())
      if (QString(dw->metaObject()->className()).contains("SelectionPanel")) { panel = dw; break; }
    if (panel) {
      QCursor::setPos(win.mapToGlobal(QPoint(win.width() - 2, win.height() / 2)));
      auto panelW = [panel] { return panel->isVisible() ? panel->width() : 0; };
      const QList<int> pin = sample(panelW);
      const int pfull = pin.isEmpty() ? 0 : pin.last();
      if (pfull > 10) {   // reveal fired (setPos is a soft no-op on some offscreen builds)
        QVERIFY2(hasIntermediate(pin, pfull), "panel reveal popped instantly (no intermediate widths)");
        QVERIFY2(nonDecreasing(pin), "panel reveal width oscillated (flicker)");
      } else {
        qWarning("panel reveal did not fire (cursor setPos likely a no-op offscreen)");
      }
    }

    // --- Exit: everything restored, no lingering graphics effect ---
    fs->trigger();
    settle([&] { return win.isVisible() && anyBarVisible(); }, 500);
    QVERIFY(win.isVisible());
    QVERIFY(anyBarVisible());
    for (QToolBar* b : win.findChildren<QToolBar*>()) QVERIFY(b->graphicsEffect() == nullptr);
  }

  // Entering and leaving fullscreen stretches the canvas out of its old viewport box — the desktop
  // twin of the FLIP in browser motion.js. The ramp only ever ENDS on the zoom the user picked.
  void fullscreenStretchPreservesZoom() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1000, 760);
    CanvasWidget* canvas = openLoaded(win);
    QVERIFY(canvas);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    win.setZoom(0.5);
    const double chosen = canvas->getScale();
    QCOMPARE(chosen, 0.5);

    QAction* fs = actionByText(&win, "Enter Fullscreen");
    QVERIFY(fs);
    fs->trigger();
    // Outlast the ramp itself plus the bounded wait for the window manager's resize.
    QTRY_VERIFY_WITH_TIMEOUT(!win.fs.zoomAnim, 3000);
    QCOMPARE(canvas->getScale(), chosen);  // entering never changed the user's zoom

    fs->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!win.fs.zoomAnim, 3000);
    QCOMPARE(canvas->getScale(), chosen);  // …and neither did leaving
  }

  // Fullscreen pulls every toolbar out from under whatever they had in the air, and takes the logo's
  // own overlay with it: nothing may be left flying over the bare canvas.
  void fullscreenLeavesNothingBehindIt() {
    if (qApp->platformName() != QLatin1String("offscreen"))
      QSKIP("fullscreen gestures need the offscreen platform");
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    openLoaded(win);
    settle([&] { return win.canvas->hasImage(); }, 500);
    QVERIFY(win.logoBtn);
    QWidget* fx = win.logoFx;
    QVERIFY(fx);
    // The mark is up (the overlay paints the logo, blanked on the button itself).
    QTRY_VERIFY_WITH_TIMEOUT(fx->isVisible(), 2000);

    // Something is in the air when the switch happens — a control's own cloud.
    stencil::gui::DisintegrateOverlay::over(win.logoBtn, &win,
                                           stencil::gui::DisintegrateOverlay::Sweep::FALL);
    const auto cloudsUp = [&win] {
      int n = 0;
      for (const char* name : {stencil::gui::DisintegrateOverlay::OBJECT_NAME,
                               "stencilControlReveal", "stencilFilterDust"})
        for (QWidget* w : win.findChildren<QWidget*>(QString::fromLatin1(name)))
          if (w->isVisible()) ++n;
      return n;
    };
    QVERIFY2(cloudsUp() > 0, "the test's own cloud never started");

    win.toggleFullscreen();
    QTRY_VERIFY2(cloudsUp() == 0, "a cloud was left flying over the fullscreen canvas");
    QVERIFY2(!win.logoBtn->isVisible(), "fullscreen kept the header row");
    QVERIFY2(!fx->isVisible(), "the logo's mark stayed up with its button gone");

    win.toggleFullscreen();   // …and back, with the header row and its mark restored
    QTRY_VERIFY_WITH_TIMEOUT(win.logoBtn->isVisible(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(fx->isVisible(), 2000);
    beat();
  }

  // A hide that lands mid-slide must not take the half-open width as the one to come back to: an
  // edge swipe in fullscreen used to shrink the panel a step per pass, down to its minimum.
  void interruptedPanelRevealKeepsItsWidth() {
    const auto motion = withMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1400, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    settle([&] { return win.selPanel->width() > 300; }, 1000);   // the deferred resizeDocks to the default width
    const int settledWidth = win.selPanel->width();
    QVERIFY(settledWidth > 300);
    win.toggleFullscreen();
    settle([&] { return !win.fs.zoomAnim; }, 1500);
    if (win.fs.hoverTimer) win.fs.hoverTimer->stop();   // the case drives the reveal, not the cursor
    win.setPanelShown(true, true);
    settle([&] { return win.selPanel->width() > 60; }, 1000);
    QVERIFY2(win.panelAnim, "the reveal should still be sliding");
    win.setPanelShown(false, true);
    settle([&] { return !win.panelAnim; }, 2000);
    QCOMPARE(win.panelRestoreWidth, settledWidth);
    win.setPanelShown(true, true);
    settle([&] { return !win.panelAnim; }, 2000);
    QCOMPARE(win.selPanel->width(), settledWidth);
    win.toggleFullscreen();
    settle([&] { return win.selPanel->width() == settledWidth; }, 1500);
    QCOMPARE(win.selPanel->width(), settledWidth);
  }

  // The separator grip is the fullscreen panel's resize affordance too (browser #fs-panel-resizer), and it lands
  // with the panel: away through the slide, up once it settles.
  void fullscreenPanelKeepsItsGrip() {
    const auto motion = withMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1400, 900);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(win.panelGrip);
    win.toggleFullscreen();
    settle([&] { return !win.fs.zoomAnim; }, 1500);
    if (win.fs.hoverTimer) win.fs.hoverTimer->stop();
    QVERIFY(!win.panelGrip->isVisible());
    win.setPanelShown(true, true);
    settle([&] { return win.selPanel->width() > 60; }, 1000);
    QVERIFY2(!win.panelGrip->isVisible(), "the grip ran ahead of the sliding panel");
    settle([&] { return !win.panelAnim; }, 2000);
    QVERIFY2(win.panelGrip->isVisible(), "no grip on the revealed fullscreen panel");
    QVERIFY(win.selPanel->maximumWidth() > win.selPanel->minimumWidth());   // …and it can still be dragged
    win.toggleFullscreen();
    settle([&] { return !win.fs.active && !win.panelAnim; }, 1500);
    QVERIFY(win.panelGrip->isVisible());
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chromeFullscreen.gui.moc"
