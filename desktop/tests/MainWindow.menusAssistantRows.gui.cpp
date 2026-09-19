// MainWindow GUI e2e — The Assistant row itself: a checkable click that must not recurse, and its gear.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindowMenu.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Clicking a CHECKABLE row must not recurse: StayOpenMenu re-dispatches mouse events into the hosted
  // chat panel and QApplication::notify walks an unaccepted press back up into the menu.
  void contextMenuCheckableClickDoesNotRecurse() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());   // the canvas menu opens for an image, and only then
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());

    for (const char* provider : {"none", "ollama"}) {
      win.settings_.llmProvider = provider;
      QAction* showPoints = nullptr;
      for (QAction* a : win.findChildren<QAction*>())
        if (a->text() == "Show Points") showPoints = a;
      QVERIFY(showPoints && showPoints->isCheckable());
      const bool before = showPoints->isChecked();

      bool clicked = false, menuAlive = false;
      QTimer::singleShot(0, [&] {
        QMenu* menu = nullptr;
        for (int i = 0; i < 200 && !menu; ++i) {
          menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
          if (!menu) QTest::qWait(10);
        }
        if (!menu) return;
        // The exact repro: a left click on the checkable row.
        QTest::mouseClick(menu, Qt::LeftButton, {},
                          menu->actionGeometry(showPoints).center());
        clicked = true;
        menuAlive = menu->isVisible();  // checkables toggle in place
        menu->close();
      });
      win.showContextMenu(win.mapToGlobal(QPoint(400, 300)));

      QVERIFY2(clicked, "the context menu never opened");
      QVERIFY2(menuAlive, "toggling a checkable row closed the menu");
      QCOMPARE(showPoints->isChecked(), !before);  // it really flipped
      showPoints->setChecked(before);              // restore for the next pass
    }

    // The other half of the same hazard: a click on a TRANSCRIPT ROW inside the chat panel. A QLabel
    // ignores mouse presses, so the re-dispatched event propagated back up to the menu.
    win.settings_.llmProvider = "ollama";
    win.ensureChatMenuPanel();
    win.chatMirror("You", "hello there", false);
    bool rowClicked = false, subAlive = false, splitterDragged = false;
    QTimer::singleShot(0, [&] {
      QMenu* menu = nullptr;
      for (int i = 0; i < 200 && !menu; ++i) {
        menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) QTest::qWait(10);
      }
      if (!menu) return;
      QAction* parent = nullptr;
      for (QAction* a : menu->actions())
        if (a->text().startsWith("Assistant")) parent = a;
      if (!parent || !parent->menu()) { menu->close(); return; }
      menu->setActiveAction(parent);
      QTest::keyClick(menu, Qt::Key_Right);
      QMenu* sub = parent->menu();
      settle([&] { return sub->isVisible(); }, 1000);
      auto* panel = sub->findChild<QWidget*>("chatMenuPanel");
      QLabel* row = nullptr;
      if (panel)
        for (QLabel* l : panel->findChildren<QLabel*>())
          if (l->isVisible() && l->text().contains("hello there")) row = l;
      if (!row) { menu->close(); return; }
      // Would previously recurse until the stack blew up.
      QTest::mouseClick(sub, Qt::LeftButton, {}, row->mapTo(sub, row->rect().center()));
      rowClicked = true;
      subAlive = sub->isVisible() && menu->isVisible();

      // Dragging the composer splitter goes through the SAME re-dispatch, plus the move forwarding a drag
      // needs: it must not recurse either, and it must actually resize.
      auto* sp = panel->findChild<QSplitter*>("chatMenuSplitter");
      if (sp && sp->count() > 1) {
        QWidget* handle = sp->handle(1);
        const QList<int> before = sp->sizes();
        const QPoint from = handle->mapTo(sub, handle->rect().center());
        QTest::mousePress(sub, Qt::LeftButton, {}, from);
        for (int dy = -8; dy >= -40; dy -= 8)
          QTest::mouseMove(sub, from + QPoint(0, dy));
        QTest::mouseRelease(sub, Qt::LeftButton, {}, from + QPoint(0, -40));
        splitterDragged = sp->sizes().at(1) > before.at(1) && sub->isVisible();
      }
      menu->close();
    });
    win.showContextMenu(win.mapToGlobal(QPoint(400, 300)));
    QVERIFY2(rowClicked, "could not click a transcript row in the assistant submenu");
    QVERIFY2(subAlive, "clicking a transcript row closed the menu");
    QVERIFY2(splitterDragged,
             "dragging the composer splitter inside the popup did not resize it");
    beat();
  }
  // The assistant submenu's gear opens the assistant-only dialog and closes the context menu FIRST: a
  // modal must never come up under a menu that still holds the popup grab, so the dialog is deferred.
  void contextMenuAssistantGearOpensSettings() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());   // the canvas menu opens for an image, and only then
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    win.settings_.llmProvider = "ollama";

    bool settingsFound = false, menuGoneAfterClick = false, popupGrabGone = false;
    QTimer::singleShot(0, [&] {
      QMenu* menu = nullptr;
      for (int i = 0; i < 200 && !menu; ++i) {
        menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) QTest::qWait(10);
      }
      if (!menu) return;
      QAction* parent = nullptr;
      for (QAction* a : menu->actions())
        if (a->text().startsWith("Assistant")) parent = a;
      if (!parent || !parent->menu()) { menu->close(); return; }
      menu->setActiveAction(parent);
      QTest::keyClick(menu, Qt::Key_Right);
      QMenu* sub = parent->menu();
      settle([&] { return sub->isVisible(); }, 1000);
      auto* settings = sub->findChild<QAction*>("chatMenuSettings");
      settingsFound = settings != nullptr;
      if (!settings) { menu->close(); return; }
      settings->trigger();   // the overflow's Settings row, through the real handler
      menuGoneAfterClick = !menu->isVisible() && !sub->isVisible();
      popupGrabGone = QApplication::activePopupWidget() == nullptr;
    });
    win.showContextMenu(win.mapToGlobal(QPoint(400, 300)));
    QVERIFY2(settingsFound, "the assistant overflow has no Settings row");
    QVERIFY2(menuGoneAfterClick, "Settings left the context menu open");
    QVERIFY2(popupGrabGone, "the popup grab survived the Settings row");

    // The dialog opens on the next turn — poll for it, check it is the
    // assistant-only one and genuinely interactive, then dismiss.
    QString dialogName;
    bool dialogLive = false;
    QTimer::singleShot(0, [&] {
      for (int i = 0; i < 200; ++i) {
        if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
          dialogName = d->objectName();
          dialogLive = d->isVisible() && d->isEnabled() &&
                       QApplication::activePopupWidget() == nullptr;
          d->reject();
          return;
        }
        QTest::qWait(10);
      }
    });
    settle([&] { return !dialogName.isEmpty(); }, 800);
    QCOMPARE(dialogName, QString("assistantSettingsDialog"));
    QVERIFY2(dialogLive, "the assistant dialog came up hidden, disabled, or under a popup");
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.menusAssistantRows.gui.moc"
