// MainWindow GUI e2e — The context-menu assistant PANEL, the dock's mirror twin, plus the
// dock's own chrome and composer: the header row, the save disclosure, the input's key handling.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Opening the assistant puts the caret in its box — otherwise the first thing you type
  // goes to the canvas shortcuts instead of the prompt you meant to write.
  void openingTheChatFocusesItsInput() {
    MainWindow win(nullptr, false);
    openLoaded(win);
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(win.chatDock_->input_->hasFocus(), 3000);
    beat();
  }

  // ⌥⌫ in the chat box deleted the selected LINE (the canvas's deleteLine shortcut claimed the
  // chord app-wide) instead of the word behind the cursor. A focused text box owns the standard
  // editing chords; the action keeps working everywhere else.
  void wordDeleteInTheChatBoxDeletesAWordNotALine() {
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    stencil::core::Lines seeded;                 // something for deleteLine to bite on
    seeded.push_back(stencil::core::Line{{{10, 10}, {40, 40}}});
    canvas->setLines(seeded);
    canvas->selectLineByIndex(0);
    const int lines = static_cast<int>(canvas->lines().size());

    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    QPlainTextEdit* input = win.chatDock_->input_;
    input->setFocus();
    QTRY_VERIFY(input->hasFocus());
    input->setPlainText(QStringLiteral("crop the portrait"));
    input->moveCursor(QTextCursor::End);

    QTest::keyClick(input, Qt::Key_Backspace, Qt::AltModifier);

    QCOMPARE(input->toPlainText(), QStringLiteral("crop the "));
    QCOMPARE(static_cast<int>(canvas->lines().size()), lines);   // the drawing is untouched
    beat();
  }

  // Browser .chat-msg-user::before/::after parity: every settled bubble grows a
  // painted tail at the corner facing the panel centre — user right, assistant/
  // error left — and "Swap message sides" (the "…" menu item between Clear
  // history and Settings) flips BOTH the alignment and the tail side of every
  // card ALREADY on screen, not just future ones, and persists.
  void chatSwapSidesReskinsRetroactively() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // Whatever this machine's settings.json already holds — restored at the end,
    // like every other test here that touches real persisted settings.
    const bool wasSwapped = win.settings_.chatSwapSides;
    if (wasSwapped) win.chatDock_->setChatSwapSides(false);   // start from a known state
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    win.chatDock_->appendUser(QStringLiteral("hi"), {});
    win.chatDock_->appendAssistant(QStringLiteral("hello"));
    // The entrance holds each card's opacity effect and drops the claim when it lands
    // (ENTERING_PROPERTY), so that IS the slide's own completion flag.
    QTRY_VERIFY(noneEntering(win.chatDock_));
    // A short bubble's width can still settle over a couple of extra layout
    // passes after the wait above (viewport/scrollbar interplay in
    // applyChatBubbleWidths) — one more explicit re-sync makes the geometry
    // checks below deterministic instead of racing the last pixel or two of it.
    win.chatDock_->applyBubbleWidths();

    QFrame* userCard = nullptr;
    QFrame* asstCard = nullptr;
    for (QFrame* f : win.chatDock_->findChildren<QFrame*>("chatCardUser")) userCard = f;
    for (QFrame* f : win.chatDock_->findChildren<QFrame*>("chatCardAssistant")) asstCard = f;
    QVERIFY(userCard && asstCard);
    auto* transcriptLayout = qobject_cast<QVBoxLayout*>(userCard->parentWidget()->layout());
    QVERIFY(transcriptLayout);
    // QBoxLayout carries a widget's alignment on the LayoutItem, not as a
    // queryable per-widget property — find it by index (setAlignment(widget,…)
    // is the only setter Qt offers, so this is the matching getter path).
    const auto alignmentOf = [](QVBoxLayout* lay, QWidget* w) {
      return lay->itemAt(lay->indexOf(w))->alignment();
    };
    QCOMPARE(alignmentOf(transcriptLayout, userCard), Qt::AlignRight);
    QCOMPARE(alignmentOf(transcriptLayout, asstCard), Qt::AlignLeft);

    // Every role bubble carries a tail — a real widget, positioned OUTSIDE the
    // card's own box on the side its alignment implies.
    const auto tailOf = [](QFrame* card) {
      return qobject_cast<QWidget*>(card->property("chatTail").value<QObject*>());
    };
    auto* userTail = tailOf(userCard);
    auto* asstTail = tailOf(asstCard);
    QVERIFY2(userTail && userTail->isVisible(), "the user bubble has no tail");
    QVERIFY2(asstTail && asstTail->isVisible(), "the assistant bubble has no tail");
    // The tail hangs from the bubble's BOTTOM edge, flush with it, and pokes out
    // past the corner facing the panel centre — checked as an invariant (pokes
    // out on the right side, sits near the bottom), not exact pixel offsets,
    // which are the placement formula's own implementation detail.
    QVERIFY2(userTail->geometry().right() > userCard->geometry().right(),
             "the user bubble's tail must poke out past its RIGHT edge");
    QVERIFY2(qAbs(userTail->geometry().bottom() - userCard->geometry().bottom()) <= 2,
             "the tail must hang flush with the bubble's bottom edge");
    QVERIFY2(asstTail->geometry().left() < asstCard->geometry().left(),
             "the assistant bubble's tail must poke out past its LEFT edge");
    if (qEnvironmentVariableIsSet("STENCIL_GUI_SHOTS")) {
      QTest::qWait(50);
      win.chatDock_->grab().save(QString::fromLocal8Bit(qgetenv("STENCIL_GUI_SHOTS")) + "/dock-tails.png");
    }

    // Flip it — through the REAL menu action, not the setter directly, so the
    // persistence signal is exercised too.
    bool signaled = false;
    bool signaledValue = false;
    connect(win.chatDock_, &stencil::gui::ChatDock::chatSwapSidesChanged, &win,
            [&](bool on) { signaled = true; signaledValue = on; });
    auto* moreBtn = win.chatDock_->findChild<QToolButton*>("chatMore");
    QVERIFY(moreBtn && moreBtn->menu());
    QAction* swapAction = nullptr;
    for (QAction* a : moreBtn->menu()->actions())
      if (a->text() == QLatin1String("Swap message sides")) swapAction = a;
    QVERIFY2(swapAction, "no \"Swap message sides\" action in the … menu");
    swapAction->trigger();

    QVERIFY2(signaled && signaledValue, "the dock must emit chatSwapSidesChanged(true)");
    QVERIFY2(win.settings_.chatSwapSides, "MainWindow must persist the flip into settings_");
    QVERIFY(win.chatDock_->chatSwapSides());

    // The EXISTING cards moved — this is the whole point (a browser/extension
    // parity CSS class would do this for free; Qt has to re-skin by hand).
    QCOMPARE(alignmentOf(transcriptLayout, userCard), Qt::AlignLeft);
    QCOMPARE(alignmentOf(transcriptLayout, asstCard), Qt::AlignRight);
    QVERIFY2(userTail->geometry().left() < userCard->geometry().left(),
             "swapped: the user bubble's tail must have moved to poke out past its LEFT edge");
    QVERIFY2(asstTail->geometry().right() > asstCard->geometry().right(),
             "swapped: the assistant bubble's tail must have moved to poke out past its RIGHT edge");

    // A round trip through the SAME file persistence every other setting uses.
    const stencil::gui::Settings loaded = stencil::gui::fileStore::loadSettings();
    QVERIFY2(loaded.chatSwapSides, "the flip must survive a settings.json round trip");

    // …and a NEWLY opened context-menu mirror panel picks up the SAME preference,
    // not the pre-flip default.
    win.ensureChatMenuPanel();
    win.chatMirror(QStringLiteral("Assistant"), QStringLiteral("mirrored"), false);
    QTRY_VERIFY(win.chatMenuPanel_->findChild<QFrame*>("chatCardAssistant"));
    QFrame* mirroredAsst = nullptr;
    for (QFrame* f : win.chatMenuPanel_->findChildren<QFrame*>("chatCardAssistant"))
      mirroredAsst = f;
    QVERIFY2(mirroredAsst, "the mirror panel never rendered the appended row");
    auto* mirrorLayout = qobject_cast<QVBoxLayout*>(mirroredAsst->parentWidget()->layout());
    QVERIFY(mirrorLayout);
    QCOMPARE(alignmentOf(mirrorLayout, mirroredAsst), Qt::AlignRight);

    // Restore whatever this machine's settings.json held before the test, exactly
    // (an even number of clicks isn't enough if it started true).
    win.chatDock_->setChatSwapSides(wasSwapped);
    win.settings_.chatSwapSides = wasSwapped;
    stencil::gui::fileStore::saveSettings(win.settings_);
    beat();
  }

  // §12.2: the save-chats toggle has to say who can READ a saved chat, right where it is
  // offered. A server project's chat file carries the project's own access, so everyone
  // the project is shared with can read a transcript of what the user asked for in their
  // own words — which is not what "Save chats with projects" sounds like it promises.
  // LlmSettingsForm is the single host of the toggle (both the full Settings sheet and
  // the assistant-only dialog embed it), so checking the form covers both places.
  void chatSaveDisclosureSitsAtTheToggle() {
    const stencil::gui::Settings defaults;
    stencil::gui::LlmSettingsForm form(defaults,
                                       stencil::gui::LlmSettingsForm::RowMode::HIDE_ROWS);

    auto* cb = form.findChild<QCheckBox*>("llmSaveChats");
    QVERIFY2(cb, "the §12 save-chats opt-in is missing from the assistant settings");
    QVERIFY2(!cb->isChecked(), "chat persistence ships OFF — it is an explicit opt-in");

    // Visible, not hover-only: a tooltip nobody opens does not disclose anything.
    auto* hint = form.findChild<QLabel*>("llmSaveChatsHint");
    QVERIFY2(hint, "the sharing consequence has no visible label at the toggle");
    QVERIFY2(hint->text().contains("shared with"),
             qPrintable("the visible hint does not state who can read it: " + hint->text()));
    QVERIFY2(!hint->text().isEmpty() && hint->isVisibleTo(&form),
             "the hint is present but not shown alongside the checkbox");

    // The tooltip carries it too, with the local-vs-server split spelled out.
    const QString tip = cb->toolTip();
    QVERIFY2(tip.contains("shared with"), qPrintable("tooltip omits the sharing rule: " + tip));
    QVERIFY2(tip.contains("Local projects"), qPrintable("tooltip omits the local case: " + tip));

    // …and the box it sits in reads as the NEXT row, not a hole in the form: the note used
    // to hang off the outer column with no spacing of its own, well below the divider
    // already under the checkbox (user report; browser has no such gap between its own
    // .vs-checks row and .chat-cors-note).
    form.resize(420, form.sizeHint().height());
    form.show();
    QVERIFY(QTest::qWaitForWindowExposed(&form));
    auto* noteBox = form.findChild<QFrame*>("llmNoteBox");
    QVERIFY2(noteBox, "the disclosure note has no box to sit in");
    const int gap = noteBox->mapTo(&form, QPoint(0, 0)).y() - (cb->mapTo(&form, QPoint(0, 0)).y() + cb->height());
    QVERIFY2(gap >= 0 && gap < 40,
             qPrintable(QStringLiteral("checkbox-to-note gap is %1px").arg(gap)));
    beat();
  }

  // One conversation, two views: the dock and the context-menu panel must show
  // the SAME rows in the SAME order however the turns were sent — no
  // duplicates, nothing missing, in either direction. Covers the awkward
  // cases: chatting with the dock CLOSED (it must still have every card when
  // opened later) and the menu panel being created LATE (it renders the
  // existing history instead of starting blank).
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
      // Read the row's IDENTITY from the widget properties, not from the card's
      // visual structure: the bubbles carry the role as colour + side (browser
      // parity), so there is no role caption label to read.
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

  // The chat header reads as chrome, not an accent badge: the "Assistant" label and its
  // mark take the theme text colour, and the CURRENT placement button is an accent glyph on
  // a container chip — the browser's .chat-dock-btn-active { color: accent; background:
  // var(--bg-container) }, not a filled accent square.
  void chatHeaderMatchesTheBrowserChrome() {
    MainWindow win;
    win.resize(1400, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actChat_->setChecked(true);
    awaitAnim(win.chatAnim_);
    auto* dock = win.findChild<stencil::gui::ChatDock*>();
    QVERIFY2(dock, "no chat dock");
    QLabel* title = dock->findChild<QLabel*>("chatHeaderTitle");
    QVERIFY2(title, "no header title");
    const stencil::gui::Palette pal = stencil::gui::themePalette(
        stencil::gui::resolveDark(win.settings_.themeMode), win.settings_.accentColor);
    QVERIFY2(title->styleSheet().contains(pal.textMain.name()),
             qPrintable("the Assistant label is not theme text: " + title->styleSheet()));
    QVERIFY2(!title->styleSheet().contains(pal.accent.name()),
             "the Assistant label still paints in the accent");
    // The active placement chip: container ground, and never the accent fill.
    bool sawActive = false;
    for (QToolButton* b : dock->findChildren<QToolButton*>()) {
      if (b->objectName() == QLatin1String("chatJumpBtn")) continue;  // transcript jump pills, not chips
      const QString qss = b->styleSheet();
      if (!qss.contains("background:")) continue;
      sawActive = true;
      QVERIFY2(qss.contains(pal.bgContainer.name()),
               qPrintable("the active placement button is not a container chip: " + qss));
      QVERIFY2(!qss.contains(pal.accent.name()),
               qPrintable("the active placement button is still accent-filled: " + qss));
    }
    QVERIFY2(sawActive, "no placement button is marked active");
    // The glyph must actually FILL its chip. The app-wide QToolButton rule pads 5x7 and
    // reserves a border, which inside a fixed 23px button squeezed the 13px mark down to
    // ~4px — half the browser's, and the reason the row read as murky.
    {
      QWidget* bar = dock->findChild<QWidget*>("chatTitleBar");
      QVERIFY(bar);
      const QImage im = bar->grab().toImage();
      const QColor ground = im.pixelColor(im.width() - 2, 1);
      // Measured INSIDE one button's own rect, so the bold "Assistant" label cannot stand
      // in for a glyph (it did, and the negative control passed).
      QToolButton* up = nullptr;
      for (QToolButton* b : bar->findChildren<QToolButton*>())
        if (b->toolTip().startsWith("Dock top")) up = b;
      QVERIFY2(up, "no dock-top button");
      const QRect r(up->mapTo(bar, QPoint(0, 0)), up->size());
      int widest = 0, run = 0;
      for (int x = r.left(); x <= r.right(); ++x) {
        bool ink = false;
        for (int y = r.top(); y <= r.bottom() && !ink; ++y) {
          const QColor c = im.pixelColor(x, y);
          ink = qAbs(c.red() - ground.red()) + qAbs(c.green() - ground.green())
              + qAbs(c.blue() - ground.blue()) > 40;
        }
        run = ink ? run + 1 : 0;
        widest = qMax(widest, run);
      }
      QVERIFY2(widest >= 6, qPrintable(QString("the chevron is squeezed: %1px of ink in a %2px chip")
                                           .arg(widest).arg(r.width())));
    }
    // Geometry parity with .chat-hbtn: a 23px chip holding a 13px glyph, 1px apart.
    for (QToolButton* b : dock->findChildren<QToolButton*>()) {
      if (!b->toolTip().contains("Dock ") && !b->toolTip().startsWith("Float")
          && !b->toolTip().startsWith("Close")) continue;
      QCOMPARE(b->size(), QSize(23, 23));
      QCOMPARE(b->iconSize(), QSize(13, 13));
      // Never disabled: QToolButton:disabled would repaint the active chip as a dead
      // bordered square with a dimmed glyph, which is what made the row look murky.
      QVERIFY2(b->isEnabled(), qPrintable("header button is disabled: " + b->toolTip()));
      QVERIFY2(!b->styleSheet().contains("border:1px"),
               qPrintable("header button has a border: " + b->styleSheet()));
    }
  }

  // The two chat surfaces render the SAME transcript across a §7 continuation, warnings, an
  // error card, a late note — and a §12 restore of the saved conversation. The panel replays
  // what the DOCK displayed, never chatHistory_: that is the model's view, carrying the
  // continuation note ("[The working image is now …]") and the interim reply the dock
  // deliberately never showed, so restoring it verbatim put internal text on screen (and
  // only on the surface that replayed it, once the two were fed from different places).
  // Run once with both surfaces up from the start, the way the report had them, and once
  // with the panel created LATE — the lazy replay is where the two used to diverge.
  void chatPanelAndDockAgreeHoweverThePanelOpens() {
    for (const bool panelOpensLate : {false, true}) {
      MainWindow win(nullptr, false);
      win.resize(1200, 850);
      win.show();
      QVERIFY(QTest::qWaitForWindowExposed(&win));
      win.settings_.llmProvider = "ollama";
      win.settings_.llmBaseUrl = "http://localhost:11434";
      MockChatTransport mock;
      const auto wrap = [](const QString& json) {
        return QJsonDocument(QJsonObject{{"message", QJsonObject{{"content", json}}}})
            .toJson(QJsonDocument::Compact);
      };
      win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
      win.actChat_->setChecked(true);
      QTRY_VERIFY(win.chatDock_->isVisible());
      // Round 1 of the §7 turn: the reply the dock HOLDS (the continuation's
      // settled answer replaces it), so no surface may ever show it.
      const QString interim = QStringLiteral(
          "Loading it into incognito, converting to black & white and cropping to portrait now.");
      const auto showPanel = [&win] {
        win.ensureChatMenuPanel();
        win.chatMenuPanel_->setGeometry(20, 20, 340, 640);
        win.chatMenuPanel_->show();
      };
      if (!panelOpensLate) showPanel();

      // Every CARD, in order: its kind plus every text it shows (body + the notes
      // riding inside it). Comparing this catches a missing row, an extra row, a
      // note rendered as its own card, and a differing body — all at once.
      const auto cardsOf = [](QWidget* surface) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QStringList out;
        for (QFrame* f : surface->findChildren<QFrame*>()) {
          const QString kind = f->objectName();
          if (!kind.startsWith(QLatin1String("chatCard")) ||
              kind == QLatin1String("chatCardMore"))
            continue;
          QStringList texts;
          for (QLabel* l : f->findChildren<QLabel*>()) {
            const QString b = l->property("chatBody").toString();
            const QString n = l->property("chatNote").toString();
            if (!b.isEmpty()) texts << b;
            else if (!n.isEmpty()) texts << n;
          }
          if (texts.size() == 1 && texts.first() == QStringLiteral("…")) continue;  // pending
          out << kind + QStringLiteral(": ") + texts.join(QStringLiteral(" ¶ "));
        }
        return out;
      };
      const auto same = [&](const char* what) {
        settle([&] { return cardsOf(win.chatMenuPanel_) == cardsOf(win.chatDock_); }, 400);
        QVERIFY2(cardsOf(win.chatMenuPanel_) == cardsOf(win.chatDock_),
                 qPrintable(QStringLiteral("%1: the two surfaces disagree\n  dock : %2\n  panel: %3")
                                .arg(QString::fromLatin1(what),
                                     cardsOf(win.chatDock_).join(QStringLiteral(" | ")),
                                     cardsOf(win.chatMenuPanel_).join(QStringLiteral(" | ")))));
      };
      const auto noInternalText = [&](const char* what) {
        for (QWidget* s : {static_cast<QWidget*>(win.chatDock_), win.chatMenuPanel_})
          for (const QString& row : cardsOf(s)) {
            QVERIFY2(!row.contains(QStringLiteral("The working image is now")),
                     qPrintable(QStringLiteral("%1: the §7 continuation note is displayed: %2")
                                    .arg(QString::fromLatin1(what), row)));
            QVERIFY2(!row.contains(interim),
                     qPrintable(QStringLiteral("%1: the held interim reply is displayed: %2")
                                    .arg(QString::fromLatin1(what), row)));
          }
      };

      // ── 1. a §7 continuation turn: ONE settled bubble on both surfaces ──
      mock.queue.append(wrap(QStringLiteral(
          "{\"version\":1,\"reply\":\"%1\",\"actions\":[{\"op\":\"blank\",\"color\":\"#ffffff\"}]}")
                                .arg(interim)));
      const QString settled = QStringLiteral(
          "Black & white applied, cropped to a 3:4 portrait, and I traced the hair silhouette.");
      mock.queue.append(wrap(
          QStringLiteral("{\"version\":1,\"reply\":\"%1\",\"actions\":[]}").arg(settled)));
      win.onChatSend(QStringLiteral("make it b&w and crop to 3:4"));
      QTRY_VERIFY(!win.chatDock_->isBusy());
      if (panelOpensLate) showPanel();
      same("continuation turn");
      noInternalText("continuation turn");
      QCOMPARE(cardsOf(win.chatDock_).size(), 2);   // the ask + the ONE settled reply

      // ── 2. the §12 round trip: saved conversation, reopened project ──
      const QJsonObject doc = win.buildActiveChatDoc();
      const QByteArray json = QJsonDocument(doc).toJson();
      QVERIFY2(!json.contains("The working image is now"),
               "the persisted doc must not carry the §7 continuation note (§12.1)");
      QVERIFY2(!json.contains(interim.toUtf8()),
               "the persisted doc must not carry the held interim reply (§12.1)");
      bool noteInHistory = false;   // the MODEL's view is untouched by the sanitising
      for (const auto& m : win.chatHistory_)
        if (m.text.contains(QStringLiteral("The working image is now"))) noteInHistory = true;
      QVERIFY2(noteInHistory, "the continuation note must still reach the model");
      win.restoreChatFromDoc(doc);
      QTRY_COMPARE(cardsOf(win.chatDock_).size(), 2);   // the wipe finished, 2 rows came back
      same("restored conversation");
      noInternalText("restored conversation");
      QVERIFY2(cardsOf(win.chatDock_).join(QChar('\n')).contains(settled),
               "the settled reply must survive the restore");

      // ── 3. warnings fold into the reply's own bubble on both ──
      mock.queue.append(wrap(QStringLiteral(
          "{\"version\":1,\"reply\":\"tinted\",\"actions\":[{\"op\":\"filter\",\"mode\":\"bw\"},"
          "{\"op\":\"wobble\"}]}")));
      win.onChatSend(QStringLiteral("make it grey"));
      QTRY_VERIFY(!win.chatDock_->isBusy());
      same("warnings turn");
      QVERIFY2(cardsOf(win.chatDock_).join(QChar('\n')).contains(QStringLiteral("wobble")),
               "the skipped-op warning is missing");

      // ── 4. an error card (a plan that will not validate) ──
      mock.queue.append(wrap(QStringLiteral(
          "{\"version\":1,\"reply\":\"here\",\"actions\":[{\"op\":\"blank\",\"color\":\"nope\"}]}")));
      win.onChatSend(QStringLiteral("break it"));
      QTRY_VERIFY(!win.chatDock_->isBusy());
      same("error turn");
      QVERIFY2(cardsOf(win.chatMenuPanel_).join(QChar('\n'))
                   .contains(QStringLiteral("chatCardError: Could not read")),
               "the panel is missing the error card");

      // ── 5. the late notes the §3 chain and the text-only retry report ──
      win.chatLateNote(QStringLiteral("The layout self-check kept the lines."));
      win.chatNote(QStringLiteral("This model is text-only — the image was not sent."));
      same("late notes");
      // …while the attachment cap is a TOAST (browser parity: notify(…, 'info')), not a
      // transcript card: neither surface grows a row, the window's stack shows the line in
      // the accent (never the danger red), and a batch's repeated hits fold into one toast.
      const int dockRows = cardsOf(win.chatDock_).size();
      win.chatDock_->warnAttachmentCap();
      win.chatDock_->warnAttachmentCap();
      same("cap toast");
      QCOMPARE(cardsOf(win.chatDock_).size(), dockRows);
      int capToasts = 0;
      for (QLabel* l : win.findChildren<QLabel*>("toast", Qt::FindDirectChildrenOnly)) {
        if (!l->property("stencilToastText").toString().startsWith("Up to 3 images per message")) continue;
        ++capToasts;
        const auto pal = stencil::gui::themePalette(stencil::gui::resolveDark(win.settings_.themeMode), win.settings_.accentColor);
        QVERIFY2(!l->styleSheet().contains(pal.danger.name(), Qt::CaseInsensitive), "the cap toast is red");
      }
      QCOMPARE(capToasts, 1);

      // ── 6. clearing empties both ──
      win.onChatClear();
      win.chatDock_->clearConversation();
      QTRY_VERIFY(cardsOf(win.chatDock_).isEmpty());
      QTRY_VERIFY(cardsOf(win.chatMenuPanel_).isEmpty());
      win.llmClient_.reset();
    }
    beat();
  }

  // A panel created LATE must render the conversation that already happened —
  // chat in the dock first, then open the context menu for the first time.
  void chatMenuPanelRendersExistingHistory() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content",
                                 "{\"version\":1,\"reply\":\"later reply\",\"actions\":[]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    QVERIFY(!win.chatMenuPanel_);  // never built yet

    auto* chat = win.findChild<QAction*>("actChat");
    chat->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    auto* dockInput = win.chatDock_->findChild<QPlainTextEdit*>("chatInput");
    QVERIFY(dockInput);
    dockInput->setPlainText("said before the menu existed");
    QTest::keyClick(dockInput, Qt::Key_Return);
    QCOMPARE(win.chatHistory_.size(), 2);

    win.ensureChatMenuPanel();  // first time the menu is needed
    QVERIFY(win.chatMenuPanel_);
    QStringList bodies;
    for (QLabel* l : win.chatMenuPanel_->findChildren<QLabel*>())
      if (!l->property("chatRole").toString().isEmpty())
        bodies << l->property("chatBody").toString();
    QCOMPARE(bodies.size(), 2);
    QCOMPARE(bodies.at(0), QString("said before the menu existed"));
    QCOMPARE(bodies.at(1), QString("later reply"));

    win.llmClient_.reset();
    beat();
  }

  // The panel is built LAZILY and lives hidden inside a QWidgetAction, so rows
  // mirrored before it is first shown were measured against the default 100px
  // viewport and stayed collapsed to about a tenth of the panel. It re-measures
  // on the way in. Its in-flight row also animates the dock's bouncing dots
  // rather than sitting as a static "…".
  void chatMenuPanelSizesBubblesAndAnimatesPending() {
    MainWindow win(nullptr, false);
    win.resize(1200, 850);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    win.ensureChatMenuPanel();
    QVERIFY(win.chatMenuPanel_);
    // Mirrored while the panel is still HIDDEN — the state that collapsed them.
    const QString longText = QStringLiteral(
        "Loading the image into incognito, converting to black & white and cropping to "
        "portrait 3:4. Once it is done I will report back with the result.");
    for (int i = 0; i < 4; ++i) {
      win.chatMirror(QStringLiteral("You"), longText, false);
      win.chatMirror(QStringLiteral("Assistant"), longText, false);
    }
    win.chatMenuPanel_->setGeometry(20, 20, 340, 640);
    win.chatMenuPanel_->show();
    settleLayout(win.chatMenuPanel_, 300);

    auto* scroll = win.chatMenuPanel_->findChild<QScrollArea*>("chatMenuTranscript");
    QVERIFY(scroll);
    const int avail = scroll->viewport()->width();
    QVERIFY2(avail > 100, "the panel transcript never got a real width");
    int checked = 0;
    for (QFrame* card : win.chatMenuPanel_->findChildren<QFrame*>()) {
      if (!card->property("chatMoreBtn").isValid()) continue;   // rows only
      ++checked;
      QVERIFY2(card->width() > avail / 2,
               qPrintable(QString("a mirrored bubble collapsed to %1 of %2 px")
                              .arg(card->width())
                              .arg(avail)));
      QVERIFY2(card->width() <= avail, "a bubble overflowed the transcript");
    }
    QVERIFY2(checked >= 4, "no mirrored rows to measure");

    // …and the pending row animates: the shared dots widget, with its own timer.
    win.chatMirrorPending(true);
    QTRY_VERIFY2(win.chatMenuPanel_->findChild<QWidget*>(QStringLiteral("chatTypingDots")),
                 "the panel's pending row has no typing dots");
    QWidget* dots = win.chatMenuPanel_->findChild<QWidget*>(QStringLiteral("chatTypingDots"));
    QTRY_VERIFY2(dots->isVisible(), "the typing dots are not on screen");
    // It really MOVES: sample the painted frame twice.
    const QImage a = dots->grab().toImage();
    QTest::qWait(160);
    const QImage b = dots->grab().toImage();
    QVERIFY2(a != b, "the typing dots are static");
    // Stopping swaps them for the text, as in the dock.
    win.chatMirrorStopped(QStringLiteral("retry me"));
    QTRY_VERIFY2(!win.chatMenuPanel_->findChild<QWidget*>(QStringLiteral("chatTypingDots")),
                 "the dots outlived the turn");
    beat();
  }

  // The context-menu Assistant panel renders the DOCK's transcript, not a second
  // ad-hoc one: same message texts, in FULL (the old panel elided them to
  // one-line stubs), whichever surface sent them — and a clear empties both.
  void chatMenuPanelMirrorsTheDockTranscript() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    const auto wrap = [](const QString& reply) {
      return QJsonDocument(
                 QJsonObject{{"message",
                              QJsonObject{{"content",
                                           QString("{\"version\":1,\"reply\":\"%1\","
                                                   "\"actions\":[]}")
                                               .arg(reply)}}}})
          .toJson(QJsonDocument::Compact);
    };
    // Long enough that the old one-line elision would have cut it.
    const QString longUser = QStringLiteral(
        "please remove this project and then tell me what happened to the image "
        "I was looking at, in as many words as you can manage");
    const QString longReply = QStringLiteral(
        "Removed the working image and its lines; nothing was saved, so there was "
        "no project file to delete alongside it.");
    mock.response = wrap(longReply);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);

    auto* chat = win.findChild<QAction*>("actChat");
    chat->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    auto* dockInput = win.chatDock_->findChild<QPlainTextEdit*>("chatInput");
    QVERIFY(dockInput);
    dockInput->setPlainText(longUser);
    QTest::keyClick(dockInput, Qt::Key_Return);
    QTRY_COMPARE(win.chatHistory_.size(), 2);

    win.ensureChatMenuPanel();
    QVERIFY(win.chatMenuPanel_);
    // The pending "…" card and cards already handed to deleteLater are excluded
    // the way assistantBubbleTexts does it.
    const auto bodies = [](QWidget* surface) {
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      QStringList out;
      for (QLabel* l : surface->findChildren<QLabel*>()) {
        const QString b = l->property("chatBody").toString();
        if (!b.isEmpty() && b != QStringLiteral("…")) out << b;
      }
      return out;
    };
    QCOMPARE(bodies(win.chatMenuPanel_), bodies(win.chatDock_));
    QVERIFY2(bodies(win.chatMenuPanel_).contains(longReply), "the reply is missing from the panel");

    // Full text on screen, wrapped — not the old "Assistant: Op…" stub.
    bool sawFull = false;
    for (QLabel* l : win.chatMenuPanel_->findChildren<QLabel*>()) {
      if (l->property("chatBody").toString() != longReply) continue;
      sawFull = true;
      QCOMPARE(l->text(), longReply);            // never elided
      QVERIFY2(l->wordWrap(), "a panel row must wrap, not elide");
      QVERIFY2(l->parentWidget()->objectName() == QLatin1String("chatCardAssistant"),
               "the panel row is not the dock's assistant card");
    }
    QVERIFY(sawFull);

    // A message sent from the PANEL lands in both surfaces too.
    mock.response = wrap(QStringLiteral("second reply"));
    auto* menuInput = qobject_cast<QPlainTextEdit*>(win.chatMenuInput_);
    QVERIFY(menuInput);
    menuInput->setPlainText("sent from the menu");
    QTest::keyClick(menuInput, Qt::Key_Return);
    QTRY_COMPARE(win.chatHistory_.size(), 4);
    QTRY_VERIFY(bodies(win.chatDock_).contains(QStringLiteral("sent from the menu")));
    QVERIFY(bodies(win.chatMenuPanel_).contains(QStringLiteral("sent from the menu")));
    QCOMPARE(bodies(win.chatMenuPanel_), bodies(win.chatDock_));

    // Clearing the conversation empties BOTH views.
    win.onChatClear();
    win.chatDock_->clearConversation();
    QTRY_VERIFY(bodies(win.chatMenuPanel_).isEmpty());
    QTRY_VERIFY(bodies(win.chatDock_).isEmpty());

    win.llmClient_.reset();
    beat();
  }

  // §10 chatPanel: the assistant panel's OWN placement, driven by a plan — "put the
  // chat on the right and open it" is a thing users ask for out loud, hands-free
  // (browser opPlan.js chatPanel parity). The plan runs through the real parser and
  // executor, so this pins the whole path, not the target method alone.
  void chatPanelOpDocksAndOpensThePanel() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(dock && chat);
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());

    const auto run = [&win](const char* json) {
      const stencil::llm::OpPlanResult r = stencil::llm::parseOpPlan(QString::fromUtf8(json));
      QVERIFY2(r.ok, qPrintable(r.error));
      stencil::gui::ChatPlanTarget target(win);
      const stencil::llm::ExecResult res = stencil::llm::executePlan(r.plan, target);
      QVERIFY2(res.ok, qPrintable(res.error));
    };

    // A dock with no "open" moves it AND shows it — placing a panel nobody can see is
    // not what was asked for.
    run(R"({"reply":"ok","actions":[{"op":"chatPanel","dock":"right"}]})");
    QTRY_VERIFY(dock->isVisible());
    QVERIFY(chat->isChecked());
    // The open and the side switch both FLY (chatSurfaceFlight) — the area is what it
    // settles at, not what it holds mid-flight.
    QTRY_VERIFY(!win.chatAnim_);
    QVERIFY(!dock->isFloating());
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::RightDockWidgetArea);

    // …the other sides go through the same path as the title bar's own buttons.
    run(R"({"reply":"ok","actions":[{"op":"chatPanel","dock":"bottom"}]})");
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::BottomDockWidgetArea);
    // The side switch flies (chatSurfaceFlight) and its finish SHOWS the dock again —
    // let it land before asking for a close, exactly as a user's second sentence would.
    QTRY_VERIFY(!win.chatAnim_);

    // "open": false closes it and leaves the placement alone.
    run(R"({"reply":"ok","actions":[{"op":"chatPanel","open":false}]})");
    QTRY_VERIFY(!dock->isVisible());
    QVERIFY(!chat->isChecked());

    // …and "float" lifts it off the edges.
    run(R"({"reply":"ok","actions":[{"op":"chatPanel","open":true,"dock":"float"}]})");
    QTRY_VERIFY(dock->isVisible());
    QTRY_VERIFY(dock->isFloating());

    // A field-less chatPanel says nothing and is rejected by the PARSER, so no plan
    // reaches the editor at all.
    const auto bad = stencil::llm::parseOpPlan(
        QStringLiteral(R"({"reply":"ok","actions":[{"op":"chatPanel"}]})"));
    QVERIFY2(!bad.ok, "a chatPanel with neither open nor dock must not parse");
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatPanel.gui.moc"
