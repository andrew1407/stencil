// MainWindow GUI e2e — The chat surfaces staying in sync with one another.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // One conversation, two views: the dock and the context-menu panel show the SAME rows in the
  // SAME order however the turns were sent — including with the dock closed or the panel late.
  void chatSurfacesStayInSync() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());   // the canvas menu opens for an image, and only then
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content",
                                 "{\"version\":1,\"reply\":\"ok reply\",\"actions\":[]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);

    // The rendered rows of each surface, as (role, body) pairs.
    auto dockRows = [&win] {
      QList<QPair<QString, QString>> rows;
      auto* scrollArea = win.chatDock_->findChild<QScrollArea*>();
      if (!scrollArea || !scrollArea->widget()) return rows;
      // Read the row's IDENTITY from the widget properties: the bubbles carry the role as colour and
      // side (browser parity), so there is no role caption label to read.
      for (QFrame* card : scrollArea->widget()->findChildren<QFrame*>(
               QString(), Qt::FindDirectChildrenOnly)) {
        for (QLabel* l : card->findChildren<QLabel*>()) {
          const QString role = l->property("chatRole").toString();
          if (role.isEmpty()) continue;
          rows.append({role, l->property("chatBody").toString()});
          break;  // one body per card
        }
      }
      return rows;
    };
    auto menuRows = [&win] {
      QList<QPair<QString, QString>> rows;
      if (!win.chatMenuPanel_) return rows;
      for (QLabel* l : win.chatMenuPanel_->findChildren<QLabel*>()) {
        const QString role = l->property("chatRole").toString();
        if (role.isEmpty()) continue;  // the empty-state hint, not a row
        rows.append({role, l->property("chatBody").toString()});
      }
      return rows;
    };
    // Send through the menu's own input, the way a user does.
    auto sendFromMenu = [&win](const QString& text) {
      QTimer::singleShot(0, [&win, text] {
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
        if (auto* input = sub->findChild<QPlainTextEdit*>("chatMenuInput")) {
          input->setFocus();
          input->setPlainText(text);
          QTest::keyClick(sub, Qt::Key_Return);
        }
        menu->close();
      });
      win.showContextMenu(win.mapToGlobal(QPoint(400, 300)));
    };

    // ── 1. chat in the MENU while the dock is closed ──
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(chat);
    chat->setChecked(false);
    QTRY_VERIFY(!win.chatDock_->isVisible());
    sendFromMenu("from the menu");
    QCOMPARE(win.chatHistory_.size(), 2);

    // The dock was hidden throughout, yet holds the whole exchange — opening it
    // must not need a replay.
    chat->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    // QTRY_: the dock retires its pending "…" card with deleteLater (it can be
    // mid-appear-animation), so the two renderings converge a turn later.
    QTRY_COMPARE(dockRows(), menuRows());
    QCOMPARE(dockRows().size(), 2);

    // ── 2. now send from the DOCK, with both surfaces alive ──
    auto* dockInput = win.chatDock_->findChild<QPlainTextEdit*>("chatInput");
    QVERIFY(dockInput);
    dockInput->setPlainText("from the dock");
    QTest::keyClick(dockInput, Qt::Key_Return);
    QCOMPARE(win.chatHistory_.size(), 4);
    QTRY_COMPARE(dockRows(), menuRows());      // the menu saw the dock's turn
    QCOMPARE(dockRows().size(), 4);            // no duplicates on either side
    QCOMPARE(dockRows().at(2).second, QString("from the dock"));

    // ── 3. an error and a stop reach both, in order ──
    stencil::llm::LlmReply bad;
    bad.ok = false;
    bad.failure = stencil::llm::LlmFailure::HTTP;
    bad.error = "boom";
    win.onChatReply(bad);
    QTRY_COMPARE(dockRows(), menuRows());
    QCOMPARE(dockRows().last().first, QString("Error"));

    win.chatDock_->showPending();
    win.chatMirrorPending(true);
    win.chatDock_->setBusy(true);
    win.chatMirrorBusy(true);
    win.chatStopRequested_ = true;
    win.chatDock_->setBusy(false);
    win.chatMirrorBusy(false);
    stencil::llm::LlmReply canceled;
    canceled.ok = false;
    canceled.failure = stencil::llm::LlmFailure::TRANSPORT;
    canceled.error = "Operation canceled";
    win.onChatReply(canceled);
    QTRY_COMPARE(dockRows(), menuRows());
    QCOMPARE(dockRows().last().second, QString("Stopped."));
    if (qEnvironmentVariableIsSet("STENCIL_GUI_SHOTS")) {
      QTest::qWait(50);
      win.chatDock_->grab().save(QString::fromLocal8Bit(qgetenv("STENCIL_GUI_SHOTS")) + "/dock-stopped.png");
    }

    // ── 4. the dock's trash button (composer row) clears both surfaces and the history ──
    auto* clearBtn = win.chatDock_->findChild<QToolButton*>("chatClear");
    QVERIFY(clearBtn);
    QTest::mouseClick(clearBtn, Qt::LeftButton);
    QVERIFY(win.chatHistory_.isEmpty());
    QTRY_COMPARE(dockRows().size(), 0);
    QCOMPARE(menuRows().size(), 0);
    // …and BOTH empty states return, chips included (dock parity).
    auto* dockChips = win.chatDock_->findChild<QWidget*>("chatSuggest");
    auto* menuChips = win.chatMenuPanel_->findChild<QWidget*>("chatSuggest");
    QVERIFY(dockChips && menuChips);
    // Both empty states return — after their wipe, not during it (clearConversation
    // holds them back for the scatter's length, so this has to be a TRY).
    QTRY_VERIFY2_WITH_TIMEOUT(!dockChips->isHidden(),
                              "the dock's chips did not come back after Clear",
                              stencil::gui::DisintegrateOverlay::DUST_MS + 3000);
    QTRY_VERIFY2_WITH_TIMEOUT(!menuChips->isHidden(),
                              "the menu's chips did not come back after Clear",
                              stencil::gui::DisintegrateOverlay::DUST_MS + 3000);
    QCOMPARE(menuChips->findChildren<QPushButton*>("chatSuggestChip").size(), 4);

    win.llmClient_.reset();
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatPanelSync.gui.moc"
