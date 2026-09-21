// MainWindow GUI e2e — The hotkey chips a menu row wears: the icon gap they must not widen, and their shake.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindowMenu.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Under Fusion a chipped row's icon-to-label gap blows out unless the action's real shortcut() is
  // silenced for the life of the chip: QMenuPrivate reserves the shortcut column twice otherwise.
  void hotkeyChipDoesNotWidenTheIconGapUnderFusion() {
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());

    // actCopyImageOriginal, not actCopyImage: the latter is no longer a row in this popup, and
    // Original's Ctrl+Shift+C is exactly as real and as much MenuHotkeyChips' job to silence.
    const QKeySequence realShortcut = win.actCopyImageOriginal->shortcut();
    QVERIFY2(!realShortcut.isEmpty(), "actCopyImageOriginal should carry a real shortcut to chip");

    QWidget* copyBtn = win.buttonForAction(win.actCopyImage);
    QVERIFY(copyBtn);
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, copyBtn->rect().center(),
                          copyBtn->mapToGlobal(copyBtn->rect().center()));
    QApplication::sendEvent(copyBtn, &ctx);
    QMenu* menu = win.copyImageOptionsMenu;
    const bool opened = menu && menu->isVisible();
    // Captured into locals and the menu closed BEFORE any assertion: an early QVERIFY2 return must never
    // leave the menu open, or it outlives `win` and crashes on teardown (reported SIGSEGV).
    bool silencedWhileChipped = false;
    QString cachedCombo;
    if (opened) {
      // While chipped: the native shortcut is silenced (that's the actual fix)...
      silencedWhileChipped = win.actCopyImageOriginal->shortcut().isEmpty();
      // ...but the row still knows the real combo (property-cache, MenuHotkeys.hpp).
      cachedCombo = win.actCopyImageOriginal->property("stencilHotkeyCombo").toString();
      menu->close();
      QTest::qWait(50);
    }
    QVERIFY2(opened, "the copy-image options popup never opened");
    QVERIFY2(silencedWhileChipped,
             "the action's native shortcut must be cleared while its row is chipped");
    QCOMPARE(cachedCombo, realShortcut.toString(QKeySequence::NativeText));
    QCOMPARE(win.actCopyImageOriginal->shortcut(), realShortcut);   // restored once the chip is torn down
  }
  // A chipped row's keycaps shake once on hover (browser .ctx-item:hover .tip-key), read through
  // capOffset() and driven with setActiveAction(); the guard advances only on a genuinely NEW row.
  void hotkeyChipShakeFollowsTheHoveredRow() {
    enum Case { SHAKES, REPLAYS_AFTER_LEAVE, NO_RESTART_ON_RE_FIRE };
    for (const Case which : {SHAKES, REPLAYS_AFTER_LEAVE, NO_RESTART_ON_RE_FIRE}) {
      const char* name = which == SHAKES ? "shakes on hover"
                         : which == REPLAYS_AFTER_LEAVE ? "replays after a leave and return"
                                                      : "no restart on a re-fire";
      MainWindow win(nullptr, false);
      win.resize(1000, 760);
      win.show();
      QVERIFY2(QTest::qWaitForWindowExposed(&win), name);
      win.openPathFromOS(guiTestImage());
      QTRY_VERIFY2(win.findChild<CanvasWidget*>()->hasImage(), name);
      // "Current"'s own row (actCopyImageCurrentRow) only shows once something is
      // drawn — see currentRowHiddenWithNoLinesButToolbarButtonStays.
      {
        stencil::core::Line line;
        line.points.push_back({4.0, 20.0});
        line.points.push_back({36.0, 20.0});
        win.canvas->setLines({line});
        win.refreshActions();
      }

      QWidget* copyBtn = win.buttonForAction(win.actCopyImage);
      QVERIFY2(copyBtn, name);
      QContextMenuEvent ctx(QContextMenuEvent::Mouse, copyBtn->rect().center(),
                            copyBtn->mapToGlobal(copyBtn->rect().center()));
      QApplication::sendEvent(copyBtn, &ctx);
      QMenu* menu = win.copyImageOptionsMenu;
      QVERIFY2(menu && menu->isVisible(), "the copy-image options popup never opened");

      QAction* row = win.actCopyImageCurrentRow;
      QAction* other = nullptr;
      // Skip invisible rows too (actCopyImageSplit stays hidden outside compare mode): setActiveAction on
      // a row with no real geometry would not make the later move onto `row` a genuine transition.
      for (QAction* a : menu->actions())
        if (a != row && !a->isSeparator() && a->isVisible()) { other = a; break; }
      QVERIFY2(other, name);
      const QRect r = menu->actionGeometry(row);

      // NOT c->isHidden(): an invisible action still has its OWN chip widget parked where it was last
      // valid (place() hides, never destroys), so geometry().intersects() alone cannot tell them apart.
      stencil::gui::TipBody* chip = nullptr;
      for (QLabel* l : menu->findChildren<QLabel*>())
        if (auto* c = dynamic_cast<stencil::gui::TipBody*>(l))
          if (!c->isHidden() && c->geometry().intersects(r)) chip = c;
      QVERIFY2(chip, "no chip found for the Current row");
      // NOT chip->capCount(): calling it LAZILY hunts the keycap regions, which is the state wire() must
      // prime itself. The rest snapshot is grabbed with the chip exactly as wire() left it.
      const QImage rest = chip->grab().toImage();

      if (which == SHAKES) {
        bool sawNonZero = false;
        QImage midShake;
        // Land on a KNOWN different row first, so the move onto `row` is a genuine
        // transition (a freshly-opened QMenu can already be hovering its first row).
        menu->setActiveAction(other);
        menu->setActiveAction(row);
        for (int i = 0; i < 40 && !sawNonZero; ++i) {
          QTest::qWait(10);
          if (chip->capOffset() != 0) { sawNonZero = true; midShake = chip->grab().toImage(); }
        }
        menu->close();
        QVERIFY2(sawNonZero, "the chip's keycaps never moved during the shake window");
        QVERIFY2(!midShake.isNull() && midShake != rest,
                 "the shake changed capOffset() but never actually painted anything different "
                 "— the caps were never hunted, so paintEvent() had nothing to draw it with");
        continue;
      }

      if (which == REPLAYS_AFTER_LEAVE) {
        menu->setActiveAction(row);   // first hover: starts the shake
        QTRY_VERIFY2(chip->capOffset() != 0, "the shake should have started");
        // A plain wait past the cycle's own length, not QTRY on ==0: the curve crosses zero mid-cycle, so
        // QTRY would accept a passing zero-crossing as "settled" while the shake is still running.
        QTest::qWait(stencil::gui::AppTooltip::SHAKE_MS + 300);
        QCOMPARE(chip->capOffset(), 0);
        // The mouse leaves the row WITHOUT ever landing on another one — no second
        // hovered(QAction*) fires for that, only a real Leave.
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(menu, &leave);
        menu->setActiveAction(row);   // back onto the SAME row — must shake again
        QTRY_VERIFY2(chip->capOffset() != 0, "the shake should replay after the mouse came back");
        menu->close();
        continue;
      }

      // The shake curve crosses zero mid-cycle, so a single fixed-instant sample can misread a live shake
      // as settled: QTRY catches it on the way up, and the checkpoints are timed off a real clock.
      QElapsedTimer timer;
      menu->setActiveAction(other);
      timer.start();
      menu->setActiveAction(row);   // first hover: starts the shake
      QTRY_VERIFY2(chip->capOffset() != 0, "the shake should have started");
      while (timer.elapsed() < 120) QTest::qWait(10);   // well clear of the start
      menu->setActiveAction(row);   // the re-fire — must NOT restart it
      // Wait to just past the ORIGINAL shake's finish line, measured from when it started: a wrongly
      // restarted shake would still be running here, while the correctly unbothered one has settled.
      const int remaining = int(stencil::gui::AppTooltip::SHAKE_MS + 60 - timer.elapsed());
      if (remaining > 0) QTest::qWait(remaining);
      // Read the chip BEFORE closing: menu->close() tears down MenuHotkeyChips, which deletes the chip
      // widgets outright, so reading through the pointer afterwards is a use-after-free.
      const int settledOffset = chip->capOffset();
      menu->close();
      QCOMPARE(settledOffset, 0);
    }
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.menusChips.gui.moc"
