// MainWindow GUI e2e — The Stencil Script flyout, and a submenu keeping the root menu's own chips.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindowMenu.gui.hpp"
#include "../src/dialogs/script/ScriptMenuPanel.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The context menu's "Stencil Script" row is a FLYOUT, not an opener (browser js/ui/ctx/script.js):
  // typing and running leave the menu open, and the typed script outlives the menu.
  void contextMenuScriptFlyout() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());   // the canvas menu opens for an image, and only then
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    win.settings.llmProvider = "ollama";   // browser: the row sits right under the Assistant
    auto* canvas = win.findChild<CanvasWidget*>();

    // startsWith, never == : the row carries its Alt+Shift+S hint in the "\t" column.
    auto scriptRow = [](QMenu* menu) -> QAction* {
      for (QAction* a : menu->actions())
        if (a->text().startsWith("Stencil Script")) return a;
      return nullptr;
    };

    bool isFlyout = false, keptHint = false, underAssistant = false, opened = false;
    bool rowInOrder = false, runIsPrimary = false, typedThrough = false, aliveAfterTyping = false;
    QTimer::singleShot(0, [&] {
      QMenu* menu = findMenu();
      if (!menu) return;
      QAction* row = scriptRow(menu);
      if (!row) { menu->close(); return; }
      isFlyout = row->menu() != nullptr;
      keptHint = row->text().contains(QLatin1Char('\t'));
      const QList<QAction*> acts = menu->actions();
      for (int i = 1; i < acts.size(); ++i)
        if (acts.at(i) == row) underAssistant = acts.at(i - 1)->text().startsWith("Assistant");
      if (!isFlyout) { menu->close(); return; }

      // The keyboard path: → reveals the flyout, a second → drops the caret in the editor.
      menu->setActiveAction(row);
      QTest::keyClick(menu, Qt::Key_Right);
      QMenu* sub = row->menu();
      settle([&] { return sub->isVisible(); }, 1000);
      opened = sub->isVisible();
      if (!opened) { menu->close(); return; }
      QTest::keyClick(menu, Qt::Key_Right);

      auto* edit = sub->findChild<QPlainTextEdit*>("scriptMenuText");
      auto* copy = sub->findChild<QPushButton*>("scriptMenuCopy");
      auto* download = sub->findChild<QPushButton*>("scriptMenuDownload");
      auto* upload = sub->findChild<QPushButton*>("scriptMenuUpload");
      auto* clear = sub->findChild<QPushButton*>("scriptMenuClear");
      auto* run = sub->findChild<QPushButton*>("scriptMenuRun");
      if (!edit || !copy || !download || !upload || !clear || !run) { menu->close(); return; }
      // Run FIRST as the primary action; Clear LAST, clear of the way to it.
      rowInOrder = run->x() < copy->x() && copy->x() < download->x() &&
                   download->x() < upload->x() && upload->x() < clear->x();
      runIsPrimary = run->property("accentCta").toBool();

      // Typed through the menu's own re-dispatch, the way the chat composer is.
      QTest::keyClicks(sub, "@filter bw");
      typedThrough = edit->toPlainText() == QLatin1String("@filter bw");
      aliveAfterTyping = sub->isVisible() && menu->isVisible();
      menu->close();
    });
    win.showContextMenu(win.mapToGlobal(QPoint(400, 300)));
    QVERIFY2(isFlyout, "the Stencil Script row is still a plain opener, not a submenu");
    QVERIFY2(keptHint, "the Stencil Script row lost its Alt+Shift+S hint");
    QVERIFY2(underAssistant, "the script flyout is not directly under the Assistant");
    QVERIFY2(opened, "the script flyout did not open");
    QVERIFY2(rowInOrder, "the actions are not Run, Copy, Download, Upload, Clear in that order");
    QVERIFY2(runIsPrimary, "Run is not the primary action");
    QVERIFY2(typedThrough, "typing never reached the flyout's editor");
    QVERIFY2(aliveAfterTyping, "typing in the flyout closed the menu");

    // Second open: the panel is the WINDOW's, so the script is still there — and running
    // it edits the canvas without dismissing anything.
    const int linesBefore = int(canvas->allLines().size());
    bool survived = false, ran = false, aliveAfterRun = false;
    QTimer::singleShot(0, [&] {
      QMenu* menu = findMenu();
      if (!menu) return;
      QAction* row = scriptRow(menu);
      if (!row || !row->menu()) { menu->close(); return; }
      menu->setActiveAction(row);
      QTest::keyClick(menu, Qt::Key_Right);
      QMenu* sub = row->menu();
      settle([&] { return sub->isVisible(); }, 1000);
      auto* edit = sub->findChild<QPlainTextEdit*>("scriptMenuText");
      auto* run = sub->findChild<QPushButton*>("scriptMenuRun");
      if (!edit || !run) { menu->close(); return; }
      survived = edit->toPlainText() == QLatin1String("@filter bw");

      edit->setPlainText(QStringLiteral("@line (1,1) (10,1) (10,8)"));
      QTest::mouseClick(sub, Qt::LeftButton, {}, run->mapTo(sub, run->rect().center()));
      settle([&] { return int(canvas->allLines().size()) > linesBefore; }, 1000);
      ran = int(canvas->allLines().size()) == linesBefore + 1;
      aliveAfterRun = sub->isVisible() && menu->isVisible();
      menu->close();
    });
    win.showContextMenu(win.mapToGlobal(QPoint(400, 300)));
    QVERIFY2(survived, "the typed script did not survive the menu closing");
    QVERIFY2(ran, "Run did not apply the script to the canvas");
    QVERIFY2(aliveAfterRun, "running the script closed the menu");

    // Upload raises a file dialog, and Qt takes every popup down the moment one opens —
    // native or not. So the hook puts the chain BACK: same place, script row, flyout open.
    stencil::gui::asScriptMenu(win.scriptMenuPanel)->setScript(QStringLiteral("@crop 10%"));
    bool reopened = false, kept = false;
    QTimer::singleShot(600, [&] {
      // With the flyout open it IS the active popup; the whole chain goes down either way,
      // or the menu's own exec would never hand control back.
      QWidget* top = QApplication::activePopupWidget();
      auto* edit = top ? top->findChild<QPlainTextEdit*>("scriptMenuText") : nullptr;
      reopened = edit != nullptr;
      kept = edit && edit->toPlainText() == QLatin1String("@crop 10%");
      stencil::gui::closeOpenPopupMenus();
    });
    win.reopenScriptFlyout();
    QTest::qWait(2500);
    QVERIFY2(reopened, "the picker left the context menu and its script flyout closed");
    QVERIFY2(kept, "the flyout came back without the script it was holding");
    beat();
  }
  // MenuHotkeyChips shares ONE rows list across the recursive wire() tree while each level's placer
  // passes THAT level's `menu`, so every row must be checked against the menu it belongs to.
  void openingASubmenuDoesNotHideTheRootMenusOwnChips() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());   // the canvas menu opens for an image, and only then
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    QWidget* viewport = win.findChild<QScrollArea*>()->viewport();
    QVERIFY(viewport);

    // Direct children only: findChildren() recurses into the SUBMENUS, whose own
    // chips sit at their y=0 and so intersect the root menu's first row.
    auto fitChip = [](QMenu* menu, QAction* fit) -> stencil::gui::TipBody* {
      const QRect r = menu->actionGeometry(fit);
      for (QLabel* l : menu->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly))
        if (auto* c = dynamic_cast<stencil::gui::TipBody*>(l))
          if (c->geometry().intersects(r)) return c;
      return nullptr;
    };

    bool chippedBeforeSubmenu = false, styleOpened = false, chippedAfterSubmenu = false;
    QTimer::singleShot(0, [&] {
      QMenu* menu = nullptr;
      for (int i = 0; i < 200 && !menu; ++i) {
        menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) QTest::qWait(10);
      }
      if (!menu) return;
      QAction* fit = nullptr;
      for (QAction* a : menu->actions())
        if (a->text().startsWith("Fit to Window")) fit = a;
      if (!fit) { menu->close(); return; }
      auto* chip = fitChip(menu, fit);
      chippedBeforeSubmenu = chip && !chip->isHidden();

      QMenu* style = openSubByKey(menu, "Style");
      styleOpened = style != nullptr;
      QTest::qWait(200);   // past a couple of Style's own 120ms live-poll ticks

      chip = fitChip(menu, fit);
      chippedAfterSubmenu = chip && !chip->isHidden();
      if (style) style->close();
      menu->close();
    });
    QTest::mouseClick(viewport, Qt::RightButton, {}, QPoint(6, 6));
    QTest::qWait(50);

    QVERIFY2(chippedBeforeSubmenu, "Fit to Window never carried a chip to begin with");
    QVERIFY2(styleOpened, "the Style submenu never opened");
    QVERIFY2(chippedAfterSubmenu, "Fit to Window's chip was hidden by the Style submenu's own live-poll");
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.menusScript.gui.moc"
