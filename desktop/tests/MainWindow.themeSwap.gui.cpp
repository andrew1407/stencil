// MainWindow GUI e2e — The theme wipe: only on real changes, ignored mid-wipe, and its old-palette snapshot.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // A palette change gets the flood-from-the-centre wipe (support/ThemeSwapOverlay.hpp,
  // the desktop twin of themeSwap in browser/js/ui/motion.js). The contract worth pinning
  // is WHEN it plays: on a real theme/accent change, never on the boot pass or on the many
  // re-applies that resolve to the same palette — and it must always clean itself up.
  void themeSwapWipesOnlyOnRealChanges() {
    const auto motion = withMotion();   // the wipe is motion: reduced motion just restyles
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(900, 640);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // The overlay is deliberately MOC-free, so it is found by object name, not by type.
    auto overlays = [&win] {
      return win.findChildren<QWidget*>(QString::fromLatin1(ThemeSwapOverlay::OBJECT_NAME),
                                        Qt::FindDirectChildrenOnly).size();
    };
    // Boot already ran applyTheme(); nothing should be mid-wipe.
    QTRY_COMPARE(overlays(), 0);

    // Re-applying the SAME palette is a no-op, however many times it is asked for.
    win.applyTheme();
    win.applyTheme();
    QCOMPARE(overlays(), 0);

    // A real flip does wipe…
    Settings flipped = win.settings_;
    flipped.themeMode = win.paintedDark_ ? "light" : "dark";
    win.applySettings(flipped, /*persist=*/false);
    QCOMPARE(overlays(), 1);
    // …and reaps itself when the animation lands, leaving no lingering child.
    QTRY_VERIFY_WITH_TIMEOUT(overlays() == 0, 3000);

    // An accent change is a palette change too.
    Settings accented = win.settings_;
    accented.accentColor = win.settings_.accentColor == "grass" ? "violet" : "grass";
    win.applySettings(accented, /*persist=*/false);
    QCOMPARE(overlays(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(overlays() == 0, 3000);
    // The wipe is decoration: it must never swallow input from the live window under it.
    QCOMPARE(win.settings_.accentColor, accented.accentColor);
  }

  // …but ONE flip at a time. A second press while the wipe plays would restyle the window
  // under an overlay still holding the PREVIOUS snapshot, and the two palettes tear across
  // each other — hammering the toolbar button was visibly breaking the window. The browser
  // gets this free (a new view transition supersedes the one in flight); here the press is
  // dropped until the wipe has finished.
  void themeToggleIsIgnoredMidWipe() {
    const auto motion = withMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(900, 640);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto overlays = [&win] {
      return win.findChildren<QWidget*>(QString::fromLatin1(ThemeSwapOverlay::OBJECT_NAME),
                                        Qt::FindDirectChildrenOnly).size();
    };
    const QString original = win.settings_.themeMode;
    QTRY_COMPARE(overlays(), 0);

    win.toggleTheme();
    const QString mid = win.settings_.themeMode;
    QCOMPARE(overlays(), 1);
    // Dropped, not queued: two presses mid-wipe leave the palette exactly where it was.
    win.toggleTheme();
    win.toggleTheme();
    QCOMPARE(win.settings_.themeMode, mid);
    QCOMPARE(overlays(), 1);
    // …and the toggle is live again the moment the wipe has reaped itself.
    QTRY_VERIFY_WITH_TIMEOUT(overlays() == 0, 3000);
    win.toggleTheme();
    QVERIFY2(win.settings_.themeMode != mid, "the toggle stayed blocked after the wipe ended");

    // Leave the persisted theme as we found it — the settings are shared across tests.
    QTRY_VERIFY_WITH_TIMEOUT(overlays() == 0, 3000);
    auto restore = win.settings_;
    restore.themeMode = original;
    win.applySettings(restore, true);
  }

  // Nothing the restyle touched may show its new colours before the circle gets there.
  // The colour chips (updateColorSwatch) carry a palette-coloured frame, and applySettings
  // used to re-issue them BEFORE applyTheme() grabbed its snapshot — so the pickers were
  // baked into the snapshot already light while the window around them was still dark, and
  // stayed that way until the wipe finally reached them.
  void themeSwapSnapshotStillWearsTheOldPalette() {
    const auto motion = withMotion();
    MainWindow win(nullptr, /*restoreLast=*/false);
    win.resize(1100, 720);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto overlays = [&win] {
      return win.findChildren<QWidget*>(QString::fromLatin1(ThemeSwapOverlay::OBJECT_NAME),
                                        Qt::FindDirectChildrenOnly).size();
    };
    QTRY_COMPARE(overlays(), 0);
    QVERIFY(win.lineColorBtn_ && win.lineColorBtn_->isVisible());

    const QImage before = win.grab().toImage();
    Settings flipped = win.settings_;
    flipped.themeMode = win.paintedDark_ ? "light" : "dark";
    win.applySettings(flipped, /*persist=*/false);
    QCOMPARE(overlays(), 1);
    // The wipe has not ticked yet, so the whole window is still the snapshot.
    const QImage during = win.grab().toImage();

    const qreal dpr = before.devicePixelRatio();
    auto deviceRect = [dpr](QWidget* w, QWidget* top) {
      const QRect r(w->mapTo(top, QPoint(0, 0)), w->size());
      return QRect(qRound(r.x() * dpr), qRound(r.y() * dpr),
                   qRound(r.width() * dpr), qRound(r.height() * dpr));
    };
    for (QToolButton* chip : {win.lineColorBtn_, win.pointColorBtn_}) {
      const QRect r = deviceRect(chip, &win);
      QVERIFY2(before.rect().contains(r), "the chip is off-window; nothing was compared");
      QCOMPARE(during.copy(r), before.copy(r));
    }
    QTRY_VERIFY_WITH_TIMEOUT(overlays() == 0, 3000);
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.themeSwap.gui.moc"
