// MainWindow GUI e2e — The hotkey chips a menu row wears: the icon gap they must not widen, and their shake.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindowMenu.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Under Fusion a chipped row's icon-to-label gap blows out unless the action's real
  // shortcut() is silenced for the life of the chip: with already-tabbed text AND a live
  // shortcut, QMenuPrivate reserves the shortcut column twice.
  void hotkeyChipDoesNotWidenTheIconGapUnderFusion() {
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());

    // actCopyImageOriginal_, not actCopyImage_: the latter is no longer a row in this
    // popup at all (actCopyImageCurrentRow_ is, and it carries no real shortcut of its
    // own by design — see MainWindow.hpp), but Original's Ctrl+Shift+C is exactly as
    // real and exactly as much MenuHotkeyChips' job to silence while chipped.
    const QKeySequence realShortcut = win.actCopyImageOriginal_->shortcut();
    QVERIFY2(!realShortcut.isEmpty(), "actCopyImageOriginal_ should carry a real shortcut to chip");

    QWidget* copyBtn = win.buttonForAction(win.actCopyImage_);
    QVERIFY(copyBtn);
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, copyBtn->rect().center(),
                          copyBtn->mapToGlobal(copyBtn->rect().center()));
    QApplication::sendEvent(copyBtn, &ctx);
    QMenu* menu = win.copyImageOptionsMenu_;
    const bool opened = menu && menu->isVisible();
    // Captured into locals and the menu closed BEFORE any assertion — an early
    // QVERIFY2 return must never leave the menu open, or it outlives `win` and
    // crashes on teardown (exportOptionsPopupIsNotWiderThanItsContent's own comment
    // has the full story — this test used to assert first, and the FALSE this
    // regression exposed took the whole process down with it, SIGSEGV, reported).
    bool silencedWhileChipped = false;
    QString cachedCombo;
    if (opened) {
      // While chipped: the native shortcut is silenced (that's the actual fix)...
      silencedWhileChipped = win.actCopyImageOriginal_->shortcut().isEmpty();
      // ...but the row still knows the real combo (property-cache, MenuHotkeys.hpp).
      cachedCombo = win.actCopyImageOriginal_->property("stencilHotkeyCombo").toString();
      menu->close();
      QTest::qWait(50);
    }
    QVERIFY2(opened, "the copy-image options popup never opened");
    QVERIFY2(silencedWhileChipped,
             "the action's native shortcut must be cleared while its row is chipped");
    QCOMPARE(cachedCombo, realShortcut.toString(QKeySequence::NativeText));
    QCOMPARE(win.actCopyImageOriginal_->shortcut(), realShortcut);   // restored once the chip is torn down
  }
  // A chipped row's keycaps shake once on hover (browser: .ctx-item:hover .tip-key /
  // keycapShake) — verified via capOffset(), "what the tests watch" per its own comment
  // (AppTooltip.hpp), and driven with setActiveAction() rather than QTest::mouseMove: the
  // latter does not reliably reach a shown popup's own hover tracking (confirmed — it left
  // QMenu::hovered's own spy at 0 — so it isn't a usable probe for this or any other
  // hover-driven popup behaviour), exactly the limitation contextMenuRowShimmersOnHover
  // already worked around for the sibling shimmer sweep. The guard around the shake
  // advances only on a genuinely NEW row: QMenu::hovered(QAction*) re-fires for the row
  // already hovered (setActiveAction re-emits it exactly as mouse jitter does) and a mouse
  // leaving a row without landing on another never fires it again, so it resets on
  // QEvent::Leave, like MenuShimmer.hpp's RowOverlay.
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
      // "Current"'s own row (actCopyImageCurrentRow_) only shows once something is
      // drawn — see currentRowHiddenWithNoLinesButToolbarButtonStays.
      {
        stencil::core::Line line;
        line.points.push_back({4.0, 20.0});
        line.points.push_back({36.0, 20.0});
        win.canvas_->setLines({line});
        win.refreshActions();
      }

      QWidget* copyBtn = win.buttonForAction(win.actCopyImage_);
      QVERIFY2(copyBtn, name);
      QContextMenuEvent ctx(QContextMenuEvent::Mouse, copyBtn->rect().center(),
                            copyBtn->mapToGlobal(copyBtn->rect().center()));
      QApplication::sendEvent(copyBtn, &ctx);
      QMenu* menu = win.copyImageOptionsMenu_;
      QVERIFY2(menu && menu->isVisible(), "the copy-image options popup never opened");

      QAction* row = win.actCopyImageCurrentRow_;
      QAction* other = nullptr;
      // Skip invisible rows too (actCopyImageSplit_ leads this same menu but stays hidden
      // outside compare mode) — setActiveAction on a row with no real geometry wouldn't
      // make the later move onto `row` a genuine transition.
      for (QAction* a : menu->actions())
        if (a != row && !a->isSeparator() && a->isVisible()) { other = a; break; }
      QVERIFY2(other, name);
      const QRect r = menu->actionGeometry(row);

      // NOT c->isHidden(): an action that's currently invisible (e.g. "Filter Only" with no
      // filter applied) still has its OWN chip widget parked wherever it was last valid —
      // geometry().intersects() alone can't tell a genuinely-showing chip from a hidden one
      // sitting in the same spot (MenuHotkeys.hpp's place() hides, never destroys them).
      stencil::gui::TipBody* chip = nullptr;
      for (QLabel* l : menu->findChildren<QLabel*>())
        if (auto* c = dynamic_cast<stencil::gui::TipBody*>(l))
          if (!c->isHidden() && c->geometry().intersects(r)) chip = c;
      QVERIFY2(chip, "no chip found for the Current row");
      // NOT chip->capCount() here — calling it is what LAZILY hunts the keycap regions, so
      // the test would prime the state MenuHotkeys.hpp's wire() must prime ITSELF. The rest
      // snapshot is taken first, grab()ing the chip exactly as wire() left it.
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
        // A plain wait long past the cycle's own length, not QTRY on ==0: the curve crosses
        // zero mid-cycle (see the re-fire case below), so QTRY would happily accept a
        // passing zero-crossing as "settled" while the shake is still running underneath.
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

      // The shake curve crosses zero mid-cycle (it's a wiggle, not a one-way ramp), so a
      // single fixed-instant sample can land on a crossing and misread a live shake as
      // settled. QTRY catches it on the way up, and the re-fire and settle checkpoints are
      // timed off a real clock rather than guessed delays.
      QElapsedTimer timer;
      menu->setActiveAction(other);
      timer.start();
      menu->setActiveAction(row);   // first hover: starts the shake
      QTRY_VERIFY2(chip->capOffset() != 0, "the shake should have started");
      while (timer.elapsed() < 120) QTest::qWait(10);   // well clear of the start
      menu->setActiveAction(row);   // the re-fire — must NOT restart it
      // Wait to (a hair past) the ORIGINAL shake's own finish line, measured from when it
      // actually started. A wrongly-restarted shake would still be running here (its own
      // clock reset at the re-fire, well under SHAKE_MS old by this checkpoint); the
      // correctly-unbothered one has already settled back to rest.
      const int remaining = int(stencil::gui::AppTooltip::SHAKE_MS + 60 - timer.elapsed());
      if (remaining > 0) QTest::qWait(remaining);
      // Read the chip BEFORE closing: menu->close() tears down MenuHotkeyChips, which
      // deletes the chip widgets outright — reading through the pointer after that is a
      // use-after-free (previously the source of this test's own flakiness).
      const int settledOffset = chip->capOffset();
      menu->close();
      QCOMPARE(settledOffset, 0);
    }
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.menusChips.gui.moc"
