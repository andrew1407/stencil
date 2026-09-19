// MainWindow GUI e2e — The copy/download split: “With Compare” borrowing the primary gesture.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindowPaint.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // "With Compare" is a SEPARATE action (actCopyImageSplit_/actSaveImageSplit_), visible only while a
  // split compare is active, and only then does it borrow Ctrl+C/Ctrl+Shift+D from its sibling.
  void copyDownloadSplitTakesThePrimaryGesture() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(40, 40, QImage::Format_RGB32);
    img.fill(Qt::white);
    win.loadImageWithLayout(img, QJsonObject());
    // "Current"'s own row (actCopyImageCurrentRow_) only shows once something is drawn, and the
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

    // Prime MenuHotkeyChips' per-action combo cache with "Current"'s Ctrl+C BEFORE compare mode turns
    // on: the regression only shows on a SECOND open, once the shortcut has moved elsewhere.
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

    // A row's chip widget must be deleted on teardown, not left orphaned but parented and visible: on a
    // reopen with a 4th row ahead of it, a leftover chip lands on a row that has no hotkey at all.
    {
      QWidget* copyBtn = win.buttonForAction(win.actCopyImage_);
      QVERIFY(copyBtn);
      QContextMenuEvent ctx(QContextMenuEvent::Mouse, copyBtn->rect().center(),
                            copyBtn->mapToGlobal(copyBtn->rect().center()));
      QApplication::sendEvent(copyBtn, &ctx);
      QMenu* menu = win.copyImageOptionsMenu_;
      const bool opened = menu && menu->isVisible();
      // Captured into locals and the menu closed BEFORE any assertion: an early QVERIFY2 return must never
      // leave the menu open, or it outlives `win` and crashes on teardown.
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
