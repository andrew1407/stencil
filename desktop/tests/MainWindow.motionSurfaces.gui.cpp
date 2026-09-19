// MainWindow GUI e2e — The drop zones' glyphs, the app-wide control swaps, and the surface dust layer.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The drop zones paint the SAME glyphs as the browser (upload / incognito) — they used
  // to be the text characters "↑" and "◐", i.e. whatever the system font happened to have.
  void dropZonesPaintTheBrowsersGlyphs() {
    QVERIFY2(stencil::gui::hasIcon("upload"), "the saving half's glyph");
    QVERIFY2(stencil::gui::hasIcon("incognito"), "the incognito half's glyph");

    QWidget host;
    host.resize(700, 460);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    stencil::gui::DropZonesOverlay zones(&host);
    zones.showZones();
    QImage shot(zones.size(), QImage::Format_ARGB32);
    shot.fill(Qt::transparent);
    zones.render(&shot);

    // Something is actually drawn in each zone's glyph band — a mistyped icon name would
    // leave it empty, which is exactly the "no glyph at all" failure to catch.
    const auto bandHasInk = [&shot](int left, int right) {
      const int top = shot.height() / 6, bottom = shot.height() / 2;
      int ink = 0;
      for (int y = top; y < bottom; y += 2)
        for (int x = left; x < right; x += 2)
          if (qAlpha(shot.pixel(x, y)) > 40) ink++;
      return ink;
    };
    QVERIFY2(bandHasInk(20, shot.width() / 2 - 20) > 50, "the upload zone drew its glyph");
    QVERIFY2(bandHasInk(shot.width() / 2 + 20, shot.width() - 20) > 50, "and so did incognito");
    beat();
  }

  // The checkbox particle toggle and the combo value exchange are installed ONCE, on the application
  // (support/controlSwap.hpp): the real toolbar controls get them with no call site, and nothing moves.
  void controlSwapsAreInstalledAppWide() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1400, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY2(qApp->findChild<QObject*>(
                 QString::fromLatin1(stencil::gui::CONTROL_SWAP_FILTER_NAME),
                 Qt::FindDirectChildrenOnly),
             "MainWindow never installed the app-wide control-swap filter");
    auto* box = win.showPointsCheck_;
    auto* combo = win.units_.pageSize;
    QVERIFY(box && combo);
    QTRY_VERIFY(box->property(stencil::gui::CONTROL_SWAP_WIRED_PROPERTY).toBool());
    QVERIFY2(combo->property(stencil::gui::CONTROL_SWAP_WIRED_PROPERTY).toBool(),
             "a toolbar combo built before the window was shown went unwired");

    // This suite runs under STENCIL_NO_ANIM; lift it just here so the REAL motion runs in
    // the REAL window, then put it back for everything after.
    qunsetenv("STENCIL_NO_ANIM");
    const QRect boxGeom = box->geometry();
    const QRect comboGeom = combo->geometry();
    const bool was = box->isChecked();
    box->setChecked(!was);
    QCOMPARE(box->isChecked(), !was);
    QCOMPARE(box->geometry(), boxGeom);
    // Rapid toggling: the last state is the one that survives, with nothing stranded.
    bool last = false;
    for (int i = 0; i < 6; ++i) { last = i % 2 == 0; box->setChecked(last); QTest::qWait(20); }
    QTRY_VERIFY(win.findChildren<QWidget*>(
                       QString::fromLatin1(stencil::gui::CHECK_SWAP_OBJECT_NAME)).isEmpty());
    QCOMPARE(box->isChecked(), last);
    QCOMPARE(box->geometry(), boxGeom);

    if (combo->count() > 1) {
      const int other = combo->currentIndex() == 0 ? 1 : 0;
      combo->setCurrentIndex(other);
      QCOMPARE(combo->currentIndex(), other);
      QCOMPARE(combo->geometry(), comboGeom);
      QTRY_VERIFY(!stencil::gui::ValueSwapOverlay::running(combo));
      QCOMPARE(combo->currentIndex(), other);
      QVERIFY2(combo->styleSheet().isEmpty(),
               "the swap left its transparent-text override on the combo");
      QCOMPARE(combo->geometry(), comboGeom);
    }

    qputenv("STENCIL_NO_ANIM", "1");
    box->setChecked(was);
    QCOMPARE(box->isChecked(), was);   // reduced motion still changes the state
    QVERIFY(win.findChildren<QWidget*>(
                   QString::fromLatin1(stencil::gui::CHECK_SWAP_OBJECT_NAME)).isEmpty());
    beat();
  }

  // A dust flight that leaves the host must not be cropped to it (placeForSurface).
  void surfaceDustLayerCoversTheWholeFlightNotJustTheWindow() {
    using stencil::gui::DisintegrateOverlay;
    const QRect host(120, 122, 900, 620);
    const QRect dragged(879, 613, 620, 700);   // Projects dragged past the bottom-right
    const QPoint icon(300, 200);
    const QRect need = DisintegrateOverlay::surfaceLayerRect(dragged, icon);
    QVERIFY2(!host.contains(need), "the host cannot hold the flight — the layer must escape it");
    QVERIFY2(need.contains(dragged), "the layer must cover the window that is coming apart");
    QVERIFY2(need.contains(icon), "…and the point its motes are pouring into");
    QVERIFY2(need.bottom() > host.bottom() && need.right() > host.right(),
             "the cropped-off part is exactly what the layer has to reach");
    QVERIFY2(!host.contains(DisintegrateOverlay::surfaceLayerRect(QRect(260, 82, 620, 700), icon)),
             "a dialog taller than the window needs the escape too");
    QVERIFY2(host.contains(DisintegrateOverlay::surfaceLayerRect(QRect(400, 300, 200, 160), icon)),
             "a flight that fits must not pay for a window of its own");
  }

  // Offscreen's virtual screen is no real desktop, so the layer stays a child there.
  void surfaceDustStaysAChildWhenThereIsNoDesktop() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    QVERIFY(dock);
    win.actChat_->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    settleLayout(&win, 300);
    win.actChat_->setChecked(false);
    QTRY_VERIFY(surfaceFlight(&win));
    auto* fx = surfaceFlight(&win);
    QVERIFY2(fx, "the floating chat's flight did not play");
    QVERIFY2(!fx->isWindow(), "offscreen has no desktop to escape onto");
    QCOMPARE(fx->geometry(), win.rect());
  }

  // A motion mode changed WHILE a window is up governs how that window LEAVES: the close flight asks
  // the mode when it PLAYS, not when it was hung on the dialog (modalReveal.cpp CloseFlight::fly).
  void dialogCloseAsksTheMotionModeAgainOnItsWayOut() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));

    // Any flight at all — the dust cloud or the ghost it falls back to.
    struct FlightSpy : QObject {
      bool seen = false;
      bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::Show) {
          auto* w = qobject_cast<QWidget*>(o);
          if (w && (w->objectName()
                        == QLatin1String(stencil::gui::DisintegrateOverlay::OBJECT_NAME)
                    || w->objectName() == QLatin1String("stencilModalGhost")))
            seen = true;
        }
        return false;
      }
    };

    const auto flewOnClose = [&](stencil::support::MotionMode openMode,
                                 stencil::support::MotionMode closeMode) {
      stencil::support::setMotionMode(openMode);
      QDialog dlg(&win);
      dlg.resize(260, 180);
      stencil::support::revealDialog(dlg, nullptr, QRect(40, 40, 26, 26));
      dlg.show();
      QTest::qWait(80);            // the open flight, whichever mode allowed it
      stencil::support::setMotionMode(closeMode);   // …the user moves the setting…
      FlightSpy spy;
      qApp->installEventFilter(&spy);
      dlg.hide();                  // …and closes the window
      QTest::qWait(30);
      qApp->removeEventFilter(&spy);
      return spy.seen;
    };

    QVERIFY2(!flewOnClose(stencil::support::MotionMode::PARTICLES,
                          stencil::support::MotionMode::NONE),
             "motion turned OFF while the window was up: it must leave without a flight");
    QVERIFY2(flewOnClose(stencil::support::MotionMode::NONE,
                         stencil::support::MotionMode::PARTICLES),
             "motion turned ON while the window was up: it must leave WITH one");
    stencil::support::setMotionMode(stencil::support::MotionMode::PARTICLES);
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.motionSurfaces.gui.moc"
