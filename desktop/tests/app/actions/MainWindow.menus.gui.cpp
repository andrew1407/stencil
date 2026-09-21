// MainWindow GUI e2e — The Assistant entry in the canvas context menu: when it appears, and where.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindowMenu.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Present only with a provider configured, in the TOP group ahead of the drawing actions,
  // and adding no separator of its own — while the classic submenus still hover-open beside it.
  void assistantEntryAndItsSeparators() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());  // a working image, so a plan has something to hit

    // ── assistant OFF: no Assistant entry at all, nothing even built ──
    bool sawAssistantWhenOff = true, sawNormalAction = false, doubleSeparator = false;
    bool styleOpenedOff = false, filterOpenedOff = false;
    int separatorsOff = 0;
    win.settings.llmProvider = "none";
    QTimer::singleShot(0, [&] {
      QMenu* menu = findMenu();
      if (!menu) return;
      sawAssistantWhenOff = false;
      QAction* prev = nullptr;
      doubleSeparator = false;
      for (QAction* a : menu->actions()) {
        if (a->text().startsWith("Assistant")) sawAssistantWhenOff = true;
        if (a->text().contains("Fullscreen")) sawNormalAction = true;
        // Nothing dangling: the entry's trailing separator must go with it.
        if (a->isSeparator() && prev && prev->isSeparator()) doubleSeparator = true;
        if (a->isSeparator()) ++separatorsOff;
        prev = a;
      }
      styleOpenedOff = openSub(menu, "Style") != nullptr;
      filterOpenedOff = openSubByKey(menu, "Image Filter") != nullptr;
      menu->close();
    });
    win.showContextMenu(win.mapToGlobal(QPoint(400, 300)));
    QVERIFY2(!sawAssistantWhenOff, "the Assistant entry showed with the assistant off");
    QVERIFY2(sawNormalAction, "the rest of the context menu went missing");
    QVERIFY2(!win.chatMenuAction, "the chat panel was built despite provider=none");
    QVERIFY2(styleOpenedOff && filterOpenedOff, "submenus did not open (assistant off)");
    QVERIFY2(!doubleSeparator, "the hidden Assistant entry left a dangling separator");

    // ── assistant ON ──
    win.settings.llmProvider = "ollama";
    win.settings.llmBaseUrl = "http://localhost:11434";
    // A mock transport answers synchronously with a canned op-plan, so nothing
    // touches the network (the same seam LlmClient.headless.cpp uses).
    MockChatTransport mock;
    // A real op-plan in ollama's response shape. Built through QJsonDocument
    // rather than a raw string literal — moc chokes on those (empty .moc).
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"Sepia applied\","
                      "\"actions\":[{\"op\":\"filter\",\"mode\":\"sepia\"}]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient = std::make_unique<stencil::llm::LlmClient>(&mock);

    bool styleOpened = false, filterOpened = false, assistantOpened = false;
    bool tooltipByKey = false, assistantBeforeDrawing = false;
    bool noSeparatorBelowAssistant = false;
    int separatorsOn = 0;
    QList<int> dustClocks;
    QTimer::singleShot(0, [&] {
      QMenu* menu = findMenu();
      if (!menu) return;
      // The classic submenus keep working with the Assistant entry present: hover-open for two of them,
      // plus the keyboard path, which must drive the menu while nothing has focused the chat input.
      styleOpened = openSub(menu, "Style") != nullptr;
      filterOpened = openSub(menu, "Image Filter") != nullptr;

      tooltipByKey = openSubByKey(menu, "Tooltip") != nullptr;

      // Ordering: the Assistant entry belongs in the TOP group, ahead of the
      // drawing actions — not tacked on at the bottom.
      int assistantIdx = -1, startDrawIdx = -1, i = 0;
      for (QAction* a : menu->actions()) {
        if (a->text().startsWith("Assistant")) assistantIdx = i;
        if (a->text().contains("Start Drawing") || a->text().contains("Stop Drawing"))
          startDrawIdx = i;
        ++i;
      }
      assistantBeforeDrawing =
          assistantIdx >= 0 && startDrawIdx >= 0 && assistantIdx < startDrawIdx;
      // No separator directly BENEATH the entry: it sits against the drawing
      // group. (The one above, after Fit, opens the section.)
      const QList<QAction*> acts = menu->actions();
      noSeparatorBelowAssistant =
          assistantIdx >= 0 && assistantIdx + 1 < acts.size() &&
          !acts.at(assistantIdx + 1)->isSeparator();
      for (QAction* a : acts)
        if (a->isSeparator()) ++separatorsOn;

      QMenu* sub = openSub(menu, "Assistant");
      assistantOpened = sub != nullptr;
      if (!sub) { menu->close(); return; }
      auto* panel = sub->findChild<QWidget*>("chatMenuPanel");
      auto* input = sub->findChild<QPlainTextEdit*>("chatMenuInput");
      auto* sendBtn = sub->findChild<QToolButton*>("chatMenuSend");
      auto* moreBtn = sub->findChild<QToolButton*>("chatMenuMore");
      if (!panel || !input || !sendBtn || !moreBtn) { menu->close(); return; }
      dustClocks = {menu->property(stencil::support::DUST_MS_PROP).toInt(),
                    sub->property(stencil::support::DUST_MS_PROP).toInt()};  // the slower clock
      menu->close();
    });
    win.showContextMenu(win.mapToGlobal(QPoint(400, 300)));

    QVERIFY2(styleOpened, "the Image / Layout submenu stopped hover-opening");
    QVERIFY2(filterOpened, "the Image Filter submenu stopped opening");
    QVERIFY2(tooltipByKey, "keyboard navigation no longer opens a submenu");
    QVERIFY2(assistantOpened, "the Assistant submenu did not open");
    QVERIFY2(assistantBeforeDrawing,
             "the Assistant entry is not in the top group (before Start Drawing)");
    QVERIFY2(noSeparatorBelowAssistant,
             "there is still a separator directly under the Assistant entry");
    // Adding the entry must not add (or drop) a separator anywhere.
    QCOMPARE(separatorsOn, separatorsOff);
    QCOMPARE(dustClocks, QList<int>(2, stencil::support::CONTEXT_MENU_DUST_MS));

    win.llmClient.reset();  // drop the mock before it goes out of scope
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.menus.gui.moc"
