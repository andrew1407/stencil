// MainWindow GUI e2e — The copy/download split: “With Compare” borrowing the primary gesture.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindowPaint.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // FEATURE: "With Compare" is a SEPARATE action (actCopyImageSplit_/
  // actSaveImageSplit_), not a relabeling of "Current" — actCopyImage_/actSaveImage_
  // always read/perform "Current", comparing or not. The split action is only VISIBLE
  // while a split compare view is active, and only then does it borrow the real
  // Ctrl+C/Ctrl+Shift+D shortcut from its "Current" sibling (syncSplitCopyDownloadSlot()) —
  // giving the shortcut back the moment compare turns off. The literal "Filter Only" row
  // (actCopyImageTint_) is unaffected either way — it never follows compare state.
  void copyDownloadSplitTakesThePrimaryGesture() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 40, QImage::Format_RGB32);
    img.fill(Qt::white);
    win.loadImageWithLayout(img, QJsonObject());
    // "Current"'s own row (actCopyImageCurrentRow_) only shows once something is
    // drawn — see currentRowHiddenWithNoLinesButToolbarButtonStays. This test's own
    // regression block below needs it visible to find its chip.
    {
      stencil::core::Line line;
      line.points.push_back({4.0, 20.0});
      line.points.push_back({36.0, 20.0});
      win.canvas_->setLines({line});
    }

    win.refreshActions();
    QCOMPARE(win.actCopyImage_->text(), QString("Current (Tint + Lines/Points)"));
    QCOMPARE(win.actSaveImage_->text(), QString("Current (Tint + Lines/Points)"));
    QVERIFY2(!win.actCopyImageSplit_->isVisible(), "With Compare shows outside compare");
    QVERIFY2(!win.actSaveImageSplit_->isVisible(), "With Compare shows outside compare");
    const QKeySequence copyShortcut = win.actCopyImage_->shortcut();
    const QKeySequence saveShortcut = win.actSaveImage_->shortcut();
    QVERIFY2(!copyShortcut.isEmpty(), "Current should carry the real Ctrl+C outside compare");

    // Prime MenuHotkeyChips' per-action combo cache with "Current"'s Ctrl+C BEFORE
    // compare mode ever turns on — the ordinary way a user would have already opened
    // this popup at some point. The real regression only shows up on a SECOND open,
    // once the shortcut has since moved elsewhere (below).
    {
      QWidget* copyBtn = win.buttonForAction(win.actCopyImage_);
      QVERIFY(copyBtn);
      QContextMenuEvent ctx(QContextMenuEvent::Mouse, copyBtn->rect().center(),
                            copyBtn->mapToGlobal(copyBtn->rect().center()));
      QApplication::sendEvent(copyBtn, &ctx);
      QVERIFY2(win.copyImageOptionsMenu_->isVisible(), "priming popup never opened");
      win.copyImageOptionsMenu_->close();
    }

    win.canvas_->setCompareMode(QStringLiteral("vertical"));
    win.refreshActions();
    // "Current" never relabels — it's still there, unaffected, beside the new row.
    QCOMPARE(win.actCopyImage_->text(), QString("Current (Tint + Lines/Points)"));
    QCOMPARE(win.actSaveImage_->text(), QString("Current (Tint + Lines/Points)"));
    QCOMPARE(win.actCopyImageSplit_->text(), QString("With Compare"));
    QCOMPARE(win.actSaveImageSplit_->text(), QString("With Compare"));
    QVERIFY2(win.actCopyImageSplit_->isVisible(), "With Compare must show while comparing");
    QVERIFY2(win.actSaveImageSplit_->isVisible(), "With Compare must show while comparing");
    // The shortcut moved onto the split action; "Current" is left with none (Qt would
    // otherwise flag two enabled actions sharing one shortcut as ambiguous).
    QCOMPARE(win.actCopyImageSplit_->shortcut(), copyShortcut);
    QCOMPARE(win.actSaveImageSplit_->shortcut(), saveShortcut);
    QVERIFY(win.actCopyImage_->shortcut().isEmpty());
    QVERIFY(win.actSaveImage_->shortcut().isEmpty());
    QVERIFY2(win.copyImageOptionsMenu_->actions().contains(win.actCopyImageSplit_),
             "the toolbar popup never got the split row");
    QCOMPARE(win.copyImageOptionsMenu_->actions().first(), win.actCopyImageSplit_);  // leads
    QCOMPARE(win.saveImageOptionsMenu_->actions().first(), win.actSaveImageSplit_);  // leads

    // REGRESSION: MenuHotkeyChips never deleted a row's chip widget on
    // teardown (MenuHotkeys.hpp's destructor only restored the action's text/shortcut) —
    // it just sat there, orphaned but still parented (and visible) on the persistent
    // menu. Reopening the SAME popup here, now with a 4th row ahead of it shifting every
    // later row down one slot, lands the leftover chip from the earlier "priming" open
    // squarely on top of whatever row now occupies its old screen position — visually a
    // hotkey combo "still showing" on a row that has none any more.
    {
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
      // has the full story).
      bool currentChipped = false, splitChipped = false;
      if (opened) {
        const QRect currentRect = menu->actionGeometry(win.actCopyImageCurrentRow_);
        const QRect splitRect = menu->actionGeometry(win.actCopyImageSplit_);
        for (QLabel* l : menu->findChildren<QLabel*>()) {
          auto* chip = dynamic_cast<stencil::gui::TipBody*>(l);
          if (!chip || chip->isHidden()) continue;
          if (chip->geometry().intersects(currentRect)) currentChipped = true;
          if (chip->geometry().intersects(splitRect)) splitChipped = true;
        }
        menu->close();
      }
      QVERIFY2(opened, "the copy-image options popup never opened");
      QVERIFY2(!currentChipped, "Current still shows a hotkey chip while a comparison is active");
      QVERIFY2(splitChipped, "With Compare should carry the chip while comparing");
    }

    win.actCopyImageSplit_->trigger();
    const QImage copiedSplit = QGuiApplication::clipboard()->image();
    QVERIFY(!copiedSplit.isNull());
    QCOMPARE(copiedSplit, win.canvas_->renderToImage(QStringLiteral("split")));

    // "Current" stays reachable — via the menu, with no hotkey of its own right now —
    // and still means the plain edited frame, not the split, even while comparing.
    win.actCopyImage_->trigger();
    QCOMPARE(QGuiApplication::clipboard()->image(), win.canvas_->renderToImage(QStringLiteral("current")));

    // The literal "Filter Only" row never auto-switches to the split composite just
    // because a compare view is active — it keeps rendering tint-only, no overlay.
    win.actCopyImageTint_->trigger();
    QCOMPARE(QGuiApplication::clipboard()->image(), win.canvas_->renderToImage(QStringLiteral("tint")));

    // Turning compare back off hides the split action again and gives "Current" back its shortcut.
    win.canvas_->setCompareMode(QStringLiteral("none"));
    win.refreshActions();
    QCOMPARE(win.actCopyImage_->text(), QString("Current (Tint + Lines/Points)"));
    QCOMPARE(win.actSaveImage_->text(), QString("Current (Tint + Lines/Points)"));
    QVERIFY(!win.actCopyImageSplit_->isVisible());
    QVERIFY(!win.actSaveImageSplit_->isVisible());
    QCOMPARE(win.actCopyImage_->shortcut(), copyShortcut);
    QCOMPARE(win.actSaveImage_->shortcut(), saveShortcut);
    win.actCopyImage_->trigger();
    QCOMPARE(QGuiApplication::clipboard()->image(), win.canvas_->renderToImage(QStringLiteral("current")));
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.toolbarCompare.gui.moc"
