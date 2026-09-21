// MainWindow GUI e2e — The export-option popups: opening them, their width and their chips.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindowMenu.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The copy/download-image toolbar buttons open a variant-options popup on right-click instead of
  // re-running the plain action (browser js/ui/exportOptionsMenu.js): wireExportOptionsPopups().
  void toolbarImageButtonsOpenExportOptionsOnRightClick() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());

    QWidget* saveBtn = win.buttonForAction(win.actSaveImage);
    QWidget* copyBtn = win.buttonForAction(win.actCopyImage);
    QVERIFY(saveBtn);
    QVERIFY(copyBtn);
    QVERIFY(win.saveImageOptionsMenu);
    QVERIFY(win.copyImageOptionsMenu);
    QVERIFY(win.saveImageOptionsMenu->actions().contains(win.actSaveImageCurrentRow));
    QVERIFY(win.saveImageOptionsMenu->actions().contains(win.actSaveImageOriginal));
    QVERIFY(win.saveImageOptionsMenu->actions().contains(win.actSaveImageTint));
    QVERIFY(win.copyImageOptionsMenu->actions().contains(win.actCopyImageCurrentRow));
    QVERIFY(win.copyImageOptionsMenu->actions().contains(win.actCopyImageOriginal));
    QVERIFY(win.copyImageOptionsMenu->actions().contains(win.actCopyImageTint));

    // Right-click the Download button: the popup opens, the plain action does NOT fire
    // (a real download would pop a blocking file dialog — this must never happen here).
    int saveTriggers = 0;
    connect(win.actSaveImage, &QAction::triggered, &win, [&] { ++saveTriggers; });
    QContextMenuEvent saveCtx(QContextMenuEvent::Mouse, saveBtn->rect().center(),
                              saveBtn->mapToGlobal(saveBtn->rect().center()));
    QApplication::sendEvent(saveBtn, &saveCtx);
    QVERIFY2(QApplication::activePopupWidget() == win.saveImageOptionsMenu,
             "right-click on the download-image button opened no popup, or the wrong one");
    QCOMPARE(saveTriggers, 0);
    win.saveImageOptionsMenu->close();

    // Same gesture on the Copy button.
    QContextMenuEvent copyCtx(QContextMenuEvent::Mouse, copyBtn->rect().center(),
                              copyBtn->mapToGlobal(copyBtn->rect().center()));
    QApplication::sendEvent(copyBtn, &copyCtx);
    QVERIFY2(QApplication::activePopupWidget() == win.copyImageOptionsMenu,
             "right-click on the copy-image button opened no popup, or the wrong one");
    win.copyImageOptionsMenu->close();

    // A plain single click on Copy still runs the default ("current") variant — deferred
    // briefly (so a following dblclick could still cancel it, though none comes here).
    int copyTriggers = 0;
    connect(win.actCopyImage, &QAction::triggered, &win, [&] { ++copyTriggers; });
    QTest::mouseClick(copyBtn, Qt::LeftButton);
    QTRY_COMPARE(copyTriggers, 1);
    beat();
  }
  // Alt+hover over the copy/download-image buttons opens their export-options popup — the same
  // hold-to-peek every popover icon gets — and releasing Alt closes it unless the cursor engaged it.
  void altHoldOverExportButtonOpensItsOptionsPopup() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // QCursor::pos() is one process-wide value outliving any window, and a stray Alt over a popover
    // button opens a modal that blocks forever in execMaybePopover: park it away from every icon.
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() - 5, win.height() - 5)));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());

    QWidget* copyBtn = win.buttonForAction(win.actCopyImage);
    QVERIFY(copyBtn);
    QMenu* menu = win.copyImageOptionsMenu;
    QVERIFY(menu && !menu->isVisible());

    // underMouse() backs up the real cursor-position check (MainWindowEvents.cpp) —
    // same state a real resting pointer leaves, and what an offscreen test can mock.
    copyBtn->setAttribute(Qt::WA_UnderMouse, true);
    QTest::keyPress(&win, Qt::Key_Alt);
    QVERIFY2(menu->isVisible(), "Alt-hover over the copy button never opened its options popup");

    // NOT engaged (cursor stayed on the button, never moved into the popup): the
    // release closes it, same as any other hold-to-peek icon.
    QTest::keyRelease(&win, Qt::Key_Alt);
    QVERIFY2(!menu->isVisible(), "releasing Alt over the button did not close the peeked popup");
    copyBtn->setAttribute(Qt::WA_UnderMouse, false);

    // ENGAGED: move the cursor onto the popup itself before releasing Alt — it
    // must survive, exactly like every other peeked popover.
    copyBtn->setAttribute(Qt::WA_UnderMouse, true);
    QTest::keyPress(&win, Qt::Key_Alt);
    QVERIFY(menu->isVisible());
    QCursor::setPos(menu->mapToGlobal(menu->rect().center()));
    QTest::qWait(20);
    copyBtn->setAttribute(Qt::WA_UnderMouse, false);
    QTest::keyRelease(&win, Qt::Key_Alt);
    QVERIFY2(menu->isVisible(), "an ENGAGED peek (cursor moved into the popup) must survive Alt release");
    menu->close();
    QTest::qWait(260);   // let the row-preview's own dust settle before `win` dies (see above)
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() - 5, win.height() - 5)));   // leave it parked for whatever runs next
  }
  // The variant popups take MenuHotkeyChips' `compact` mode rather than theme.cpp's generic QMenu::item
  // padding, which is sized for the menu bar's wider rows. This bounds the SLACK only.
  void exportOptionsPopupIsNotWiderThanItsContent() {
    QApplication::setStyle(QStyleFactory::create("Fusion"));   // main.cpp forces this app-wide
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());

    QWidget* copyBtn = win.buttonForAction(win.actCopyImage);
    QVERIFY(copyBtn);
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, copyBtn->rect().center(),
                          copyBtn->mapToGlobal(copyBtn->rect().center()));
    QApplication::sendEvent(copyBtn, &ctx);
    QMenu* menu = win.copyImageOptionsMenu;
    QVERIFY2(menu && menu->isVisible(), "the copy-image options popup never opened");

    int widestLabel = 0;
    for (QAction* a : menu->actions()) {
      if (!a->isVisible()) continue;   // e.g. "Filter Only" with no filter applied
      const QString label = a->text().left(a->text().indexOf('\t'));
      widestLabel = std::max(widestLabel, menu->fontMetrics().horizontalAdvance(label));
    }
    int widestChip = 0;
    // Skip HIDDEN chips ("Filter Only" with no filter, "With Compare" outside compare): place() hides
    // rather than destroys them, so one can sit there with a width that never shows on screen.
    for (QLabel* l : menu->findChildren<QLabel*>())
      if (auto* chip = dynamic_cast<stencil::gui::TipBody*>(l))
        if (!chip->isHidden()) widestChip = std::max(widestChip, chip->width());
    QVERIFY2(widestChip > 0, "no hotkey chips found on the copy-image popup");

    // Icon + paddings + the gap between label and chip + the menu's own frame: a generous ceiling, not
    // an exact match, since it only has to catch a row coming out FAR wider than its content.
    const int slack = menu->width() - (widestLabel + widestChip);
    // Closed BEFORE asserting: an early QVERIFY2 return must never leave the menu open, or it outlives
    // `win` and crashes on teardown (reported SIGSEGV).
    menu->close();
    QVERIFY2(slack > 0 && slack <= 80,
             qPrintable(QString("menu is %1 wide for a %2px label + %3px chip — %4px of slack")
                            .arg(menu->width()).arg(widestLabel).arg(widestChip).arg(slack)));
  }
  // Per-chip "does it actually fit inside the menu", not the aggregate slack above: setFixedWidth clips
  // the outer frame only, never QMenuPrivate's sizeHint-driven row layout. Closed before asserting.
  void downloadPopupChipsAreNotClipped() {
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());

    QWidget* saveBtn = win.buttonForAction(win.actSaveImage);
    QVERIFY(saveBtn);
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, saveBtn->rect().center(),
                          saveBtn->mapToGlobal(saveBtn->rect().center()));
    QApplication::sendEvent(saveBtn, &ctx);
    QMenu* menu = win.saveImageOptionsMenu;
    const bool opened = menu && menu->isVisible();
    bool anyOverflow = false;
    if (opened) {
      QTest::qWait(60);
      for (QLabel* l : menu->findChildren<QLabel*>()) {
        if (auto* chip = dynamic_cast<stencil::gui::TipBody*>(l))
          if (!chip->isHidden() && chip->geometry().right() > menu->width()) anyOverflow = true;
      }
      menu->close();
      QTest::qWait(50);
    }
    QVERIFY2(opened, "the download-image options popup never opened");
    QVERIFY2(!anyOverflow, "a chip's right edge overflows the menu's own width");
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.menusExport.gui.moc"
