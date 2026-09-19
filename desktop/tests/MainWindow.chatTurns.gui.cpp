// MainWindow GUI e2e — A turn through the assistant: cards and their row menus, attachments, edge
// maps, continuations, plan execution and the transcript they leave behind.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // While an assistant turn is in flight, the send button becomes STOP
  // (enabled, "Stop the response"), Enter is a no-op (single-turn guard), and
  // clicking STOP turns the pending "…" card into a muted "Stopped." without
  // an assistant history push or a toast; the composer then returns to normal.
  void chatStopWhileBusy() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* chat = win.findChild<QAction*>("actChat");
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    QVERIFY(chat && dock);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    auto* input = dock->findChild<QPlainTextEdit*>("chatInput");
    auto* send = dock->findChild<QToolButton*>("chatSend");
    QVERIFY(input && send);

    // Simulate an in-flight turn: exactly the state onChatSend sets up.
    win.chatDock_->showPending();
    win.chatDock_->setBusy(true);
    QVERIFY(send->isEnabled());  // STOP mode is always clickable
    QCOMPARE(send->toolTip(), QString("Stop the response"));

    // Enter while busy is ignored (single-turn): the input keeps its text and
    // no send fires (a send would clear it).
    input->setPlainText("second question");
    QTest::keyClick(input, Qt::Key_Return);
    QCOMPARE(input->toPlainText(), QString("second question"));
    input->clear();

    // Click STOP → the abort flag is set; then the canceled reply lands.
    const int histBefore = win.chatHistory_.size();
    QTest::mouseClick(send, Qt::LeftButton);
    QVERIFY(win.chatStopRequested_);
    win.chatDock_->setBusy(false);  // what the chat completion wrapper does
    stencil::llm::LlmReply canceled;
    canceled.ok = false;
    canceled.failure = stencil::llm::LlmFailure::TRANSPORT;
    canceled.error = "Operation canceled";
    win.onChatReply(canceled);

    // The pending card became "Stopped."; nothing was pushed or toasted.
    bool stoppedShown = false;
    for (QLabel* l : dock->findChildren<QLabel*>())
      if (l->text() == QString("Stopped.")) stoppedShown = true;
    QVERIFY(stoppedShown);
    QCOMPARE(win.chatHistory_.size(), histBefore);
    auto* toast = win.findChild<QWidget*>("chatToast");
    QVERIFY(!toast || !toast->isVisible());

    // Composer back to normal: send glyph/tooltip restored, guard cleared.
    QVERIFY(!win.chatStopRequested_);
    QCOMPARE(send->toolTip(), QString());
    QVERIFY(!send->isEnabled());  // idle + empty input gates send again
    input->setPlainText("hello");
    QVERIFY(send->isEnabled());
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    beat();
  }

  void chatClearConversation() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* chat = win.findChild<QAction*>("actChat");
    auto* dock = qobject_cast<stencil::gui::ChatDock*>(
        win.findChild<QDockWidget*>("llmChatDock"));
    QVERIFY(chat && dock);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    QTRY_VERIFY(dock->width() > 200);  // let the open slide settle before geometry

    // The button lives in the COMPOSER row (browser #chat-clear parity), after
    // the settings gear — NOT in the title bar with the placement chevrons.
    QWidget* title = dock->titleBarWidget();
    QVERIFY(title);
    QVERIFY(!title->findChild<QToolButton*>("chatClear"));
    auto* clearBtn = dock->findChild<QToolButton*>("chatClear");
    QVERIFY(clearBtn);
    QVERIFY(clearBtn->toolTip().isEmpty());  // no tooltip — the menu label says it (user decision)
    // Both live in the … menu now — hidden buttons have no meaningful geometry,
    // so their identity is what matters, not their x order.
    auto* gearBtn = dock->findChild<QToolButton*>("chatGear");
    QVERIFY(gearBtn);
    QVERIFY(gearBtn->isHidden() && clearBtn->isHidden());

    // A conversation: two cards, an attachment, and the model-side state a
    // turn leaves behind.
    auto* scrollArea = dock->findChild<QScrollArea*>();
    QVERIFY(scrollArea && scrollArea->widget());
    QWidget* transcript = scrollArea->widget();
    const auto cardCount = [transcript] {
      // Cards are the only direct QFrame children of the transcript column
      // (the suggestion block is a plain QWidget).
      return transcript->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly)
          .size();
    };
    dock->appendUser("make it sepia");
    dock->appendAssistant("done");
    QCOMPARE(cardCount(), 2);
    QImage att(8, 8, QImage::Format_RGB32);
    att.fill(Qt::blue);
    dock->addAttachmentImage(att);
    QCOMPARE(dock->attachedImages().size(), 1);

    // A drag onto the COMPOSER attaches; the cue shows while it hovers and goes on
    // drop. Driven through the composer's own event filter, which is where the whole
    // gesture now lives (the dock itself declines drops).
    {
      auto* inputArea = dock->findChild<QWidget*>("chatInputArea");
      auto* cue = dock->findChild<QWidget*>("chatDropCue");
      auto* inputBox = dock->findChild<QPlainTextEdit*>("chatInput");
      QVERIFY(inputArea && cue && inputBox);
      // Aim at the INPUT BOX: the cue (and the attach) belong to it alone — a drag
      // over the composer's buttons or the chip tray neither lights nor attaches.
      const QPoint at = inputBox->mapTo(inputArea, inputBox->rect().center());
      QImage dragged(12, 12, QImage::Format_RGB32);
      dragged.fill(Qt::red);
      QMimeData mime;
      mime.setImageData(dragged);
      const int before = dock->attachedImages().size();
      QDragEnterEvent enter(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
      QVERIFY(qApp->sendEvent(inputArea, &enter));
      QVERIFY2(enter.isAccepted(), "the composer refused an image drag");
      QVERIFY2(!cue->isHidden(), "no drop cue while a drag hovers the composer");
      if (qEnvironmentVariableIsSet("STENCIL_GUI_SHOTS")) {
        QTest::qWait(50);
        inputArea->grab().save(QString::fromLocal8Bit(qgetenv("STENCIL_GUI_SHOTS")) + "/dock-dropcue.png");
      }
      // Off the box (the buttons' corner) the cue goes out mid-drag…
      const QPoint offBox(inputArea->width() - 3, inputArea->height() - 3);
      QDragMoveEvent wander(offBox, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
      QVERIFY(qApp->sendEvent(inputArea, &wander));
      QVERIFY2(cue->isHidden(), "the cue lit over the composer's buttons");
      // …and back over it the cue returns; the drop there attaches.
      QDragMoveEvent back(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
      QVERIFY(qApp->sendEvent(inputArea, &back));
      QVERIFY2(!cue->isHidden(), "the cue did not come back over the input box");
      QDropEvent drop(QPointF(at), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
      QVERIFY(qApp->sendEvent(inputArea, &drop));
      QVERIFY2(drop.isAccepted(), "the composer refused an image drop");
      QCOMPARE(dock->attachedImages().size(), before + 1);
      QVERIFY2(cue->isHidden(), "the cue outlived the drop");
      // …and the queued chip's thumbnail carries a hover preview (28px tells you
      // nothing about which screenshot it is).
      auto* tray = dock->findChild<QWidget*>("chatAttachTray");
      QVERIFY(tray);
      bool previewed = false;
      for (QLabel* pic : tray->findChildren<QLabel*>())
        if (!pic->pixmap().isNull() && !pic->findChildren<QObject*>().isEmpty()) previewed = true;
      QVERIFY2(previewed, "no hover preview installed on the attachment chip");
      // A NAMED attachment says its name; only an unnamed one (a dropped bitmap, as
      // above) falls back to the dimensions. And the thumbnail carries no tooltip —
      // it opens the hover preview, and a tooltip on top of that put two popups on
      // screen at once, the tooltip covering the picture it was describing.
      dock->clearAttachments();
      dock->addAttachmentImage(dragged, QStringLiteral("cat.png"));
      QStringList chipTexts;
      bool thumbHasTooltip = false;
      for (QLabel* l : tray->findChildren<QLabel*>()) {
        if (l->pixmap().isNull()) chipTexts << l->text();
        else if (!l->toolTip().isEmpty()) thumbHasTooltip = true;
      }
      QVERIFY2(chipTexts.contains("cat.png"), qPrintable("chip shows: " + chipTexts.join('|')));
      QVERIFY2(!thumbHasTooltip, "the thumbnail must not duplicate the hover preview in a tooltip");
      // Put the queue back the way the surrounding case staged it (one image).
      dock->clearAttachments();
      dock->addAttachmentImage(att);
    }

    // What the user attached is SHOWN, inside the user's own bubble: the card
    // carries a pixmap label (it used to say "[1 image(s) attached]" and nothing
    // more). The browser/extension render the same strip.
    {
      const auto before =
          transcript->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly);
      dock->appendUser("and this one", dock->attachedImages());
      const auto after =
          transcript->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly);
      QCOMPARE(after.size(), before.size() + 1);
      QFrame* userCard = after.last();
      QCOMPARE(userCard->objectName(), QStringLiteral("chatCardUser"));
      int thumbs = 0;
      QString body;
      for (QLabel* l : userCard->findChildren<QLabel*>()) {
        if (!l->pixmap().isNull()) thumbs++;
        else body += l->text();
      }
      QCOMPARE(thumbs, 1);
      QVERIFY(body.contains("and this one"));
      QVERIFY(!body.contains("image(s) attached"));   // the count-only text is gone
      // A turn with nothing attached stays a plain bubble.
      dock->appendUser("no images here");
      QFrame* plain =
          transcript->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly).last();
      int plainThumbs = 0;
      for (QLabel* l : plain->findChildren<QLabel*>())
        if (!l->pixmap().isNull()) plainThumbs++;
      QCOMPARE(plainThumbs, 0);
    }
    stencil::llm::ChatMessage m;
    m.role = "user";
    m.text = "make it sepia";
    win.pushChatHistory(m);
    win.chatVideoPath_ = "/tmp/clip.mp4";
    win.chatVideoFrames_ = 42;
    win.chatImageDigest_ = QByteArray("digest");
    win.chatImageEncoded_.data = QByteArray("cached");
    QVERIFY(!win.chatHistory_.isEmpty());
    auto* suggest = dock->findChild<QWidget*>("chatSuggest");
    QVERIFY(suggest && !suggest->isVisible());  // hidden by the first card

    // Frozen while a turn is in flight (like attach).
    auto* attachBtn = dock->findChild<QToolButton*>("chatAttach");
    QVERIFY(attachBtn);
    dock->setBusy(true);
    QVERIFY(!clearBtn->isEnabled());
    QVERIFY(!attachBtn->isEnabled());
    dock->setBusy(false);
    QVERIFY(clearBtn->isEnabled());

    // Click: transcript emptied, chips back, attachments + model state dropped;
    // the provider config survives.
    const QString providerBefore = win.currentLlmSettings().provider;
    QTest::mouseClick(clearBtn, Qt::LeftButton);
    QTRY_COMPARE(cardCount(), 0);   // cards go through deleteLater
    // The empty state is held back for the length of the scatter: showing it in the
    // same tick put the chips under particles that were still falling, and the clear
    // read as happening twice (browser chatView.js restoreEmptyState parity). The wait
    // is keyed off rows being REMOVED, not off the scatter animating — offscreen (and
    // on a hidden dock) there are no particles, and the empty state must still not
    // beat the wipe.
    QVERIFY2(!suggest->isVisible(), "the chips came back before the wipe finished");
    QTRY_VERIFY_WITH_TIMEOUT(suggest->isVisible(),
                             stencil::gui::DisintegrateOverlay::DUST_MS + 3000);
    QCOMPARE(dock->attachedImages().size(), 0);
    QVERIFY(dock->attachedVideoPath().isEmpty());
    QVERIFY(win.chatHistory_.isEmpty());
    QVERIFY(win.chatVideoPath_.isEmpty());
    QCOMPARE(win.chatVideoFrames_, 0);
    QVERIFY(win.chatImageDigest_.isEmpty());
    QVERIFY(win.chatImageEncoded_.data.isEmpty());
    QCOMPARE(win.currentLlmSettings().provider, providerBefore);

    // Appear: a fresh card is claimed by its own opacity effect and ends fully visible.
    // Overlapping appends each own their animation, so all of them land at 1.0 and at
    // their resting margins. This suite runs REDUCED (STENCIL_NO_ANIM), where the card is
    // simply THERE — the arrival's veil-then-dust is chatCardsArriveOutOfDust's business,
    // and a card left hidden here is what reducedMotionChatCardArrivesAtOnce pins.
    dock->appendUser("again");
    dock->appendAssistant("sure");
    dock->appendError("nope");
    const auto cards =
        transcript->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly);
    QCOMPARE(cards.size(), 3);
    for (QFrame* card : cards) {
      auto* fx = qobject_cast<QGraphicsOpacityEffect*>(card->graphicsEffect());
      QTRY_COMPARE(fx->opacity(), 1.0);
      QVERIFY(card->layout());
      QTRY_COMPARE(card->layout()->contentsMargins().top(), 6);  // slide resolved
    }
    // Clearing mid-animation must not crash (the card owns its animation).
    dock->appendAssistant("mid-flight");
    QTest::mouseClick(clearBtn, Qt::LeftButton);
    QTRY_COMPARE(cardCount(), 0);
    QTest::qWait(200);  // let any surviving animation tick would-be-dangling

    // A card that SCROLLS while it fades must not crash. ScrollReveal installs its own
    // DissolveEffect on cards near a viewport edge, and setGraphicsEffect deletes the
    // effect already there — so the fade's animation was left writing to freed memory
    // and the app died in QGraphicsOpacityEffect::setOpacity one frame later. The fade
    // now claims the card (ENTERING_PROPERTY) exactly as the entrance animation does.
    {
      for (int i = 0; i < 6; i++) {
        dock->appendUser(QStringLiteral("question %1").arg(i));
        dock->appendAssistant(QStringLiteral("a reply long enough to wrap and take real height %1").arg(i));
      }
      // Every entrance has landed and dropped its own claim on the effect — otherwise
      // the assertion below passes for the wrong reason.
      QTRY_VERIFY(noneEntering(transcript));
      dock->clearConversation();          // every card starts fading…
      // …and the INVARIANT holds from the first frame: every leaving card claims its
      // graphics effect, which is the flag ScrollReveal::apply() skips on. Without the
      // claim ScrollReveal calls setGraphicsEffect on a fading card, Qt deletes the
      // effect the fade's animation writes to, and the next frame is a use-after-free
      // (the crash the app died of). The crash itself cannot be reproduced offscreen:
      // the transcript never becomes scrollable there, so ScrollReveal returns before
      // installing anything — hence the invariant, not the symptom.
      int claimed = 0, fading = 0;
      for (QFrame* card : transcript->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (!card->graphicsEffect()) continue;
        fading++;
        if (card->property(stencil::gui::ScrollReveal::ENTERING_PROPERTY).toBool()) claimed++;
      }
      QVERIFY2(fading > 0, "no card was actually fading — the guard would be vacuous");
      QCOMPARE(claimed, fading);
      for (int i = 0; i < 12; i++) {      // …while the transcript relayouts under them
        dock->resize(dock->width(), 300 + (i % 3) * 60);
        QTest::qWait(16);                 // one driver frame per relayout, no more
      }
      QTRY_VERIFY_WITH_TIMEOUT(cardCount() == 0,
                               stencil::gui::DisintegrateOverlay::DUST_MS + 3000);
    }

    // A LONG wrapped reply must not be cut off by its own bubble: the label's wrapped
    // height is RESERVED (applyBubbleWidths), because heightForWidth is only a hint and
    // the transcript's layout does not re-ask once it has sized a card. The reply used
    // to end mid-sentence at the card's bottom edge.
    {
      const QString essay =
          QStringLiteral("The layout is drawn on a 794x1123 px page, so the rectangle sits at "
                         "roughly 250,400-544,723 — tell me if the centre is off. The three "
                         "variants are rotated, tinted and cropped. I couldn't open an "
                         "incognito tab: that needs a URL you gave me in this conversation.");
      dock->appendAssistant(essay);
      // The appear animation offsets the card's margins; it drops its own claim when it
      // lands (ENTERING_PROPERTY), and the reserved wrap height follows one relayout later.
      QTRY_VERIFY(noneEntering(transcript));
      settleLayout(transcript, 250);
      QLabel* body = nullptr;
      for (QLabel* l : transcript->findChildren<QLabel*>())
        if (l->text() == essay) body = l;
      QVERIFY(body);
      const int wrapped = body->heightForWidth(body->width());
      QVERIFY2(body->height() >= wrapped,
               qPrintable(QStringLiteral("the reply is clipped: %1px tall for %2px of text")
                              .arg(body->height()).arg(wrapped)));
      QVERIFY2(body->parentWidget()->height() >= body->height(),
               "the bubble is shorter than the text inside it");
    }

    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    beat();
  }

  // Chat persistence (llm-contract.md §12): with the opt-in ON a settled
  // conversation is filed on the active LOCAL project's record, reopening the
  // project replays it into the history + dock cards, the trash also deletes
  // the persisted copy, and with the opt-in OFF (the default) nothing is
  // written. Uses the friend seam to drive the persist/restore pipeline
  // directly (no mock LLM needed — the seam sits after the reply parsing).
  void chatPersistsWithProject() {
    using stencil::gui::fileStore::parseChatDoc;
    using stencil::gui::Project;
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    win.settings_.saveChatsWithProject = true;

    // A local project to file the chat under.
    win.adoptCanvasAsLocalProject();
    QVERIFY(!win.activeProjectId_.isEmpty());
    const QString projectId = win.activeProjectId_;

    // A settled turn: history push + the persist that onChatReply's tail runs.
    stencil::llm::ChatMessage u;
    u.role = "user";
    u.text = "make it sepia";
    stencil::llm::ChatMessage a;
    a.role = "assistant";
    a.text = "Sepia applied.";
    win.pushChatHistory(u);
    win.pushChatHistory(a);
    // The doc is built from what was DISPLAYED (§12.1), so mirror the two rows
    // the send/reply paths would have posted.
    win.chatMirror("You", u.text, false);
    win.chatMirror("Assistant", a.text, false);
    win.persistActiveChat();
    {
      Project* pr = win.findProject(projectId.toStdString());
      QVERIFY(pr);
      QCOMPARE(parseChatDoc(pr->chat).size(), 2);   // saved, text-only, in order
    }

    // Reopening the project replays the saved conversation: replay history AND
    // dock transcript cards (restoreChatFromDoc via loadProjectIntoCanvas).
    win.resetChatState();
    QVERIFY(win.chatHistory_.isEmpty());
    QVERIFY(win.loadProjectIntoCanvas(projectId));
    QCOMPARE(win.chatHistory_.size(), 2);
    QCOMPARE(win.chatHistory_.last().text, QString("Sepia applied."));
    {
      auto* dock = qobject_cast<stencil::gui::ChatDock*>(
          win.findChild<QDockWidget*>("llmChatDock"));
      QVERIFY(dock);
      auto* scrollArea = dock->findChild<QScrollArea*>();
      QVERIFY(scrollArea && scrollArea->widget());
      const auto cards = scrollArea->widget()->findChildren<QFrame*>(
          QString(), Qt::FindDirectChildrenOnly);
      QCOMPARE(cards.size(), 2);   // one card per restored turn
    }

    // The trash clears the persisted copy too (§12.2).
    win.onChatClear();
    QVERIFY(win.chatHistory_.isEmpty());
    {
      Project* pr = win.findProject(projectId.toStdString());
      QVERIFY(pr && pr->chat.isEmpty());
    }

    // Opt-in OFF (the default): a turn leaves the record untouched.
    win.settings_.saveChatsWithProject = false;
    win.pushChatHistory(u);
    win.persistActiveChat();
    {
      Project* pr = win.findProject(projectId.toStdString());
      QVERIFY(pr && pr->chat.isEmpty());
    }

    // Tidy the dev state dir: drop the project this test created.
    dismissModal("OK");
    QAction* clear = actionByText(&win, "Clear Project");
    QVERIFY(clear);
    clear->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!canvas->hasImage(), 5000);
    beat();
  }

  // §12.1 WRITE side: the persisted document is the DISPLAYED transcript, never
  // chatHistory_ — that is the model's view, carrying the §7 continuation note
  // and the held round-1 reply the dock never showed. The doc rides the .stencil
  // file and the server "chat" kind to the browser, the bot and the consoles, so
  // internal text written here can no longer be filtered out anywhere.
  void chatDocSavesOnlyTheDisplayedTranscript() {
    using stencil::gui::fileStore::parseChatDoc;
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
    win.ensureChatMenuPanel();
    win.chatMenuPanel_->setGeometry(20, 20, 340, 640);
    win.chatMenuPanel_->show();

    // Every card's body + in-card notes, per surface — the displayed transcript.
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

    // A §7 continuation turn: round 1 loads without tracing, so its reply is
    // HELD and round 2's settled answer is the only bubble the user gets.
    const QString interim = QStringLiteral("Loading the blank page and cropping now.");
    const QString settled = QStringLiteral("Blank page ready, converted to black & white.");
    const QString ask = QStringLiteral("give me a blank page in b&w");
    mock.queue.append(wrap(QStringLiteral(
        "{\"version\":1,\"reply\":\"%1\",\"actions\":[{\"op\":\"blank\",\"color\":\"#ffffff\"}]}")
                              .arg(interim)));
    mock.queue.append(wrap(
        QStringLiteral("{\"version\":1,\"reply\":\"%1\",\"actions\":[]}").arg(settled)));
    win.onChatSend(ask);
    QTRY_VERIFY(!win.chatDock_->isBusy());
    QTest::qWait(200);

    // ── 1. the document is exactly the two displayed rows ──
    const QJsonObject doc = win.buildActiveChatDoc();
    const QByteArray json = QJsonDocument(doc).toJson();
    QVERIFY2(!json.contains("The working image is now"),
             qPrintable("the §7 continuation note was persisted: " + QString::fromUtf8(json)));
    QVERIFY2(!json.contains(interim.toUtf8()),
             qPrintable("the held interim reply was persisted: " + QString::fromUtf8(json)));
    const QJsonArray saved = parseChatDoc(doc);
    QCOMPARE(saved.size(), 2);
    QCOMPARE(saved.at(0).toObject().value("role").toString(), QString("user"));
    QCOMPARE(saved.at(0).toObject().value("text").toString(), ask);
    QCOMPARE(saved.at(1).toObject().value("role").toString(), QString("assistant"));
    QCOMPARE(saved.at(1).toObject().value("text").toString(), settled);
    QCOMPARE(doc.value("version").toInt(), 1);
    QVERIFY(doc.value("savedAt").toDouble() > 0);
    // The MODEL's view is untouched: the live conversation still replays both.
    bool noteInHistory = false, interimInHistory = false;
    for (const auto& m : win.chatHistory_) {
      if (m.text.contains(QStringLiteral("The working image is now"))) noteInHistory = true;
      if (m.text == interim) interimInHistory = true;
    }
    QVERIFY2(noteInHistory && interimInHistory,
             "chatHistory_ must keep the full model-side history");

    // ── 2. it round-trips to the same transcript on BOTH surfaces ──
    const QStringList before = cardsOf(win.chatDock_);
    win.restoreChatFromDoc(doc);
    QTRY_COMPARE(cardsOf(win.chatDock_).size(), 2);
    QCOMPARE(cardsOf(win.chatDock_), before);
    QCOMPARE(cardsOf(win.chatMenuPanel_), cardsOf(win.chatDock_));
    // …and re-saving the restored conversation is a fixed point.
    QCOMPARE(parseChatDoc(win.buildActiveChatDoc()), saved);

    // ── 3. defence in depth: an OLD-style doc (written before the machinery
    // filter) is laundered on read — the §7 note never resurfaces on screen or
    // in the replay history; the interim reply is indistinguishable from
    // conversation and survives (browser sanitizeChatMessages parity) ──
    QJsonArray old;
    const auto row = [](const char* role, const QString& text) {
      return QJsonObject{{"role", QString::fromLatin1(role)}, {"text", text}};
    };
    old.append(row("user", ask));
    old.append(row("assistant", interim));
    old.append(row("user", QStringLiteral(
        "[The working image is now the picture those actions loaded — continue with it.]")));
    old.append(row("assistant", settled));
    // The write side filters too: the note never even reaches a new document.
    const QJsonObject oldDoc{{"version", 1},
                             {"savedAt", 42},
                             {"messages", old}};
    QCOMPARE(stencil::gui::fileStore::buildChatDoc(old, 42).value("messages").toArray().size(), 3);
    win.restoreChatFromDoc(oldDoc);
    QTRY_COMPARE(cardsOf(win.chatDock_).size(), 3);
    QCOMPARE(cardsOf(win.chatMenuPanel_), cardsOf(win.chatDock_));
    for (const QString& r : cardsOf(win.chatDock_))
      QVERIFY2(!r.contains(QStringLiteral("The working image is now")),
               qPrintable("an old doc put the continuation note on screen: " + r));
    QCOMPARE(win.chatHistory_.size(), 3);   // the model replays the same laundered view
    bool oldNoteInHistory = false;
    for (const auto& m : win.chatHistory_)
      if (m.text.contains(QStringLiteral("The working image is now"))) oldNoteInHistory = true;
    QVERIFY2(!oldNoteInHistory, "the §7 note must not be replayed from storage");

    // ── 4. the §12.1 bound: writers trim to the most recent 32 ──
    win.onChatClear();
    win.chatDock_->clearConversation();
    for (int i = 0; i < 40; ++i)
      win.chatMirror(i % 2 ? QStringLiteral("Assistant") : QStringLiteral("You"),
                     QStringLiteral("row %1").arg(i), false);
    const QJsonArray trimmed = parseChatDoc(win.buildActiveChatDoc());
    QCOMPARE(trimmed.size(), 32);
    QCOMPARE(trimmed.at(0).toObject().value("text").toString(), QString("row 8"));

    // ── 5. muted plumbing is never conversation ──
    win.onChatClear();
    win.chatDock_->clearConversation();
    win.chatMirror(QStringLiteral("You"), ask, false);
    win.chatError(QStringLiteral("Could not read the assistant's plan: bad op"), QString());
    win.chatMirror(QStringLiteral("Attached"), QStringLiteral("1 image(s)"), true);
    const QJsonArray onlyUser = parseChatDoc(win.buildActiveChatDoc());
    QCOMPARE(onlyUser.size(), 1);
    QCOMPARE(onlyUser.at(0).toObject().value("text").toString(), ask);

    win.onChatClear();
    win.chatDock_->clearConversation();
    QVERIFY(win.buildActiveChatDoc().isEmpty());   // nothing displayed ⇒ nothing filed
    win.llmClient_.reset();
    beat();
  }

  // The error card's Resend glyph is NEUTRAL in both themes, never the card's own
  // red: the browser's retry is a .chat-hbtn, which sets `color: var(--text-muted)`
  // itself and does not inherit the bubble's --danger. Painted red it sat red-on-red
  // in the danger wash and barely read.
  void chatErrorRetryGlyphIsNeutral() {
    for (const QString mode : {QStringLiteral("light"), QStringLiteral("dark")}) {
      MainWindow win(nullptr, false);
      win.resize(1100, 760);
      win.show();
      QVERIFY(QTest::qWaitForWindowExposed(&win));
      win.settings_.themeMode = mode;
      win.applyTheme();
      win.actChat_->setChecked(true);
      QTRY_VERIFY(win.chatDock_->isVisible());
      win.chatDock_->appendError(
          QStringLiteral("Could not read the assistant's plan: \"clear\" is an "
                         "editor-settings op"),
          QStringLiteral("remove this project"));
      QToolButton* retry = win.chatDock_->findChild<QToolButton*>("chatRetry");
      QVERIFY2(retry, "the error card has no Resend button");
      QVERIFY2(retry->parentWidget()->objectName() == QLatin1String("chatCardError"),
               "the Resend button is not on the error card");
      const QImage glyph = retry->icon().pixmap(14, 14).toImage();
      QVERIFY(!glyph.isNull());
      const QColor danger = stencil::gui::themePalette(mode == "dark").danger;
      int ink = 0, red = 0;
      for (int y = 0; y < glyph.height(); ++y)
        for (int x = 0; x < glyph.width(); ++x) {
          const QColor c = glyph.pixelColor(x, y);
          if (c.alpha() < 60) continue;
          ++ink;
          if (qAbs(c.red() - danger.red()) < 45 && qAbs(c.green() - danger.green()) < 45
              && qAbs(c.blue() - danger.blue()) < 45)
            ++red;
        }
      QVERIFY2(ink > 0, qPrintable(mode + ": the Resend glyph rendered nothing"));
      QVERIFY2(red == 0, qPrintable(mode + ": the Resend glyph is still danger red"));
      beat();
    }
  }

  // A 401 from the SERVER provider is an expired session, not a broken assistant:
  // the card says which server and carries a labelled "Reconnect to <host>" that
  // opens Connections. A local provider's 401 stays an ordinary error card.
  void chatExpiredSessionCardOffersReconnect() {
    MainWindow win(nullptr, false);
    win.resize(1200, 820);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    win.chatHistory_.append({QStringLiteral("user"), QStringLiteral("crop it"), {}});

    stencil::llm::LlmReply expired;
    expired.ok = false;
    expired.failure = stencil::llm::LlmFailure::EXPIRED;
    expired.expiredHost = QStringLiteral("localhost:8090");
    expired.error = QStringLiteral(
        "Your session on localhost:8090 has expired — reconnect to that server, then "
        "send this again.");
    win.onChatReply(expired);
    QTRY_VERIFY2(win.chatDock_->findChild<QFrame*>("chatCardError"),
                 "no error card for the expired session");
    QFrame* card = nullptr;
    for (QFrame* f : win.chatDock_->findChildren<QFrame*>("chatCardError")) card = f;
    QVERIFY2(card, "no error card for the expired session");
    bool saidIt = false;
    for (QLabel* l : card->findChildren<QLabel*>())
      if (l->property("chatBody").toString().contains(QStringLiteral("has expired")) &&
          l->property("chatBody").toString().contains(QStringLiteral("localhost:8090")))
        saidIt = true;
    QVERIFY2(saidIt, "the card does not name the server or say the session expired");
    auto* cta = card->findChild<QPushButton*>(QStringLiteral("chatReconnectCta"));
    QVERIFY2(cta, "no Reconnect CTA on the expired card");
    QCOMPARE(cta->text(), QStringLiteral("Reconnect to localhost:8090"));
    QVERIFY2(card->findChild<QToolButton*>("chatRetry"),
             "the turn should still be resendable after signing in");

    // The CTA opens Connections (dismissed straight away here).
    bool opened = false;
    QTimer::singleShot(0, [&opened] {
      for (int i = 0; i < 80; ++i) {
        if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
          opened = true;
          d->reject();
          return;
        }
        QTest::qWait(5);
      }
    });
    cta->click();
    QTRY_VERIFY2(opened, "the CTA did not open Connections");

    // …and an ordinary failure keeps the plain card (no CTA).
    stencil::llm::LlmReply plain;
    plain.ok = false;
    plain.failure = stencil::llm::LlmFailure::HTTP;
    plain.error = QStringLiteral("localhost:11434 answered: HTTP 401");
    win.onChatReply(plain);
    QTRY_COMPARE(win.chatDock_->findChildren<QFrame*>("chatCardError").size(), 2);
    QFrame* last = nullptr;
    for (QFrame* f : win.chatDock_->findChildren<QFrame*>("chatCardError")) last = f;
    QVERIFY(last);
    QVERIFY2(!last->findChild<QPushButton*>(QStringLiteral("chatReconnectCta")),
             "a local provider's 401 must not offer a server reconnect");
    beat();
  }

  void chatShowsModelTextLiterally() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";

    // The img is what would reach QTextDocument's resource loader if interpreted.
    const QString reply = QStringLiteral("<b>done</b> <img src=\"/etc/passwd\">");
    const QString question = QStringLiteral("<i>Which</i> one?");
    const QString option = QStringLiteral("<u>the first</u>");

    MockChatTransport mock;
    const QJsonObject plan{
        {"version", 1},
        {"reply", reply},
        // Nothing to run: asking INSTEAD of acting is the §11 case, and the
        // chat-only path must still render the card. Options carry no "actions"
        // either, so it needs no loaded image for previews.
        {"actions", QJsonArray{}},
        {"ask", QJsonObject{{"question", question},
                            {"mode", "single"},
                            {"options", QJsonArray{QJsonObject{{"label", option}},
                                                   QJsonObject{{"label", "the second"}}}}}},
    };
    mock.response =
        QJsonDocument(QJsonObject{
                          {"message",
                           QJsonObject{{"content", QString::fromUtf8(
                                                       QJsonDocument(plan).toJson(QJsonDocument::Compact))}}}})
            .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);

    auto* chat = win.findChild<QAction*>("actChat");
    chat->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    auto* dockInput = win.chatDock_->findChild<QPlainTextEdit*>("chatInput");
    QVERIFY(dockInput);
    dockInput->setPlainText("go");
    QTest::keyClick(dockInput, Qt::Key_Return);
    QCOMPARE(win.chatHistory_.size(), 2);
    win.ensureChatMenuPanel();
    QVERIFY(win.chatMenuPanel_);

    // Every transcript row, on BOTH surfaces, is identified by its property —
    // not by where it sits — so a restyle can't quietly drop this from cover.
    int bodies = 0;
    for (QWidget* surface : {static_cast<QWidget*>(win.chatDock_),
                             static_cast<QWidget*>(win.chatMenuPanel_)}) {
      for (QLabel* l : surface->findChildren<QLabel*>()) {
        if (l->property("chatBody").toString().isEmpty()) continue;
        ++bodies;
        QVERIFY2(l->textFormat() == Qt::PlainText,
                 qPrintable(QStringLiteral("a transcript row renders model text as %1, not PlainText: %2")
                                .arg(int(l->textFormat()))
                                .arg(l->property("chatBody").toString())));
      }
    }
    QVERIFY2(bodies >= 4, "expected the user + assistant row on each of the two surfaces");

    // The assistant row shows the tags themselves. Interpreted markup would
    // leave text() holding the source while the SCREEN showed "done" in bold —
    // so assert the format above AND the round-trip here.
    bool sawReply = false, sawQuestion = false, sawOption = false;
    for (QLabel* l : win.chatDock_->findChildren<QLabel*>()) {
      if (l->text() == reply) { sawReply = true; QCOMPARE(l->textFormat(), Qt::PlainText); }
      if (l->text() == question) { sawQuestion = true; QCOMPARE(l->textFormat(), Qt::PlainText); }
      if (l->text() == option) { sawOption = true; QCOMPARE(l->textFormat(), Qt::PlainText); }
    }
    QVERIFY2(sawReply, "the assistant reply is not on screen as the literal text the model sent");
    QVERIFY2(sawQuestion, "the ask card's question is not on screen as literal text");
    QVERIFY2(sawOption, "the ask card's option label is not on screen as literal text");

    // The menu mirror carries the FULL text in the row itself (the dock's card
    // rendering — no tooltip, no elision), so the round-trip holds there too.
    bool checkedMirror = false;
    for (QLabel* l : win.chatMenuPanel_->findChildren<QLabel*>()) {
      if (l->property("chatBody").toString() != reply) continue;
      checkedMirror = true;
      QCOMPARE(l->text(), reply);
      QCOMPARE(l->textFormat(), Qt::PlainText);
    }
    QVERIFY2(checkedMirror, "no mirrored row carried the assistant reply");

    win.llmClient_.reset();
    beat();
  }

  // An executor note about SUCCESSFUL work ("Opened X in the editor first…") rides
  // INSIDE the assistant's reply bubble as muted text — one assistant card per turn,
  // never a second card in the red error treatment (browser parity: the note merges
  // into the reply's warnings). Standalone notes (appendNote/appendNotice) render on
  // the muted card style with the reply bubble's paddings; danger stays reserved for
  // actual turn errors.
  void chatExecutorNoteRidesWithReply() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* chat = win.findChild<QAction*>("actChat");
    auto* dock = qobject_cast<stencil::gui::ChatDock*>(
        win.findChild<QDockWidget*>("llmChatDock"));
    QVERIFY(chat && dock);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    QTRY_VERIFY(dock->width() > 200);
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"Making it black and white.\",\"actions\":"
                      "[{\"op\":\"filter\",\"mode\":\"bw\"}]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);

    // An editing plan arriving with an EMPTY canvas + an attachment adopts the
    // attachment as the working image and says so (the adoption note).
    QVERIFY(!win.canvas_->hasImage());
    QImage att(64, 48, QImage::Format_RGB32);
    att.fill(Qt::darkCyan);
    dock->addAttachmentImage(att, QStringLiteral("cat.png"));
    win.onChatSend("make it b&w");
    QTRY_VERIFY(win.canvas_->hasImage());

    auto* scrollArea = dock->findChild<QScrollArea*>();
    QVERIFY(scrollArea && scrollArea->widget());
    const auto cards = [scrollArea] {
      return scrollArea->widget()->findChildren<QFrame*>(QString(),
                                                         Qt::FindDirectChildrenOnly);
    };
    // ONE assistant bubble for the whole turn: user card + assistant card, and the
    // note is not a card of its own (it used to land as a chatCardError bubble).
    QTRY_COMPARE(cards().size(), 2);
    QFrame* reply = cards().last();
    QCOMPARE(reply->objectName(), QStringLiteral("chatCardAssistant"));
    QLabel* body = nullptr;
    QLabel* note = nullptr;
    for (QLabel* l : reply->findChildren<QLabel*>()) {
      if (!l->property("chatBody").toString().isEmpty()) body = l;
      if (!l->property("chatNote").toString().isEmpty()) note = l;
    }
    QVERIFY(body && note);
    QCOMPARE(body->property("chatBody").toString(),
             QString("Making it black and white."));
    QVERIFY(note->property("chatNote").toString().startsWith("Opened cat.png"));
    // The note is NEUTRAL: the muted stylesheet tone (QSS beats palettes here, so
    // the colour rides the chatNoteLabel rule), never the danger treatment.
    QCOMPARE(note->objectName(), QStringLiteral("chatNoteLabel"));
    QVERIFY(dock->styleSheet().contains("QLabel#chatNoteLabel{color:"));

    // Standalone notes (text-only retry, outline-refine) keep their own card, on
    // the MUTED style — and with exactly the reply bubble's vertical paddings, so
    // the same text renders at the same card height.
    const QString sample =
        QStringLiteral("A note long enough to wrap over a couple of lines in the dock.");
    dock->appendNote(sample);
    dock->appendAssistant(sample);
    QTRY_VERIFY(noneEntering(scrollArea->widget()));   // the appear animations landed
    const auto after = cards();
    QCOMPARE(after.size(), 4);
    QFrame* noteCard = after.at(2);
    QFrame* bubbleCard = after.at(3);
    QCOMPARE(noteCard->objectName(), QStringLiteral("chatCardMuted"));
    QVERIFY(dock->styleSheet().contains("#chatCardMuted QLabel{color:"));
    QCOMPARE(noteCard->layout()->contentsMargins(),
             bubbleCard->layout()->contentsMargins());
    QCOMPARE(noteCard->height(), bubbleCard->height());

    // The "assistant off" notice is the muted treatment too, never the red row.
    dock->appendNotice("The assistant is turned off.");
    QCOMPARE(cards().last()->objectName(), QStringLiteral("chatCardMuted"));

    if (qEnvironmentVariableIsSet("STENCIL_GUI_SHOTS")) {
      QTest::qWait(50);
      dock->grab().save(QString::fromLocal8Bit(qgetenv("STENCIL_GUI_SHOTS")) +
                        "/dock-executor-note.png");
    }
    beat();
  }

  // §10 new editor rows end-to-end: one mock-transport plan drives the compare
  // view (mode + split), the zoom, and a rename of the active saved project —
  // through the SAME setters the toolbar uses, so the combo/canvas/registry all
  // agree afterwards.
  void chatComparZoomRenamePlanDrivesEditor() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(64, 48, QImage::Format_RGB32);
    img.fill(Qt::darkYellow);
    win.loadImageWithLayout(img, QJsonObject());
    // A saved ACTIVE project, uniquely named per run (renameProjectById
    // validates against the persisted registry).
    const QString base =
        QStringLiteral("chat view src %1").arg(QDateTime::currentMSecsSinceEpoch());
    win.createLocalProject(base, /*announce=*/false);
    QVERIFY(!win.activeProjectId_.isEmpty());

    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    const QString renamed = base + QStringLiteral(" renamed");
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      QStringLiteral(
                          "{\"version\":1,\"reply\":\"View set\",\"actions\":["
                          "{\"op\":\"compare\",\"mode\":\"vertical\",\"split\":0.3},"
                          "{\"op\":\"zoom\",\"percent\":150},"
                          "{\"op\":\"renameProject\",\"name\":\"%1\"}]}")
                          .arg(renamed)}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    const QSize sizeBefore = win.canvas_->image().size();
    win.onChatSend("compare it side by side, zoom in, and rename the project");

    // The compare view: canvas mode + divider, and the toolbar combo followed.
    QTRY_COMPARE(win.canvas_->compareMode(), QStringLiteral("vertical"));
    QCOMPARE(win.canvas_->compareSplit(), 0.3);
    QCOMPARE(win.compareCombo_->currentData().toString(), QStringLiteral("vertical"));
    // The zoom landed on the canvas scale (view-only — the image is untouched).
    QVERIFY(std::abs(win.canvas_->scale() - 1.5) < 1e-9);
    QCOMPARE(win.canvas_->image().size(), sizeBefore);
    // The rename went through the real registry path.
    QCOMPARE(win.activeProjectName(), renamed);
    bool inRegistry = false;
    for (const auto& p : win.projectList_)
      if (p.meta.name == renamed.toStdString()) inRegistry = true;
    QVERIFY2(inRegistry, "the renamed project is in the persisted registry");
    // The turn resolved as ONE assistant bubble with the plan's reply.
    auto* dock = qobject_cast<stencil::gui::ChatDock*>(
        win.findChild<QDockWidget*>("llmChatDock"));
    QVERIFY(dock);
    QTRY_VERIFY(assistantBubbleTexts(dock).contains(QStringLiteral("View set")));
    // Leave the shared registry tidy for the other cases.
    const QString id = win.activeProjectId_;
    win.setCompareModeUi(QStringLiteral("none"));
    win.eraseLocalProject(id);
    stencil::gui::fileStore::saveProjects(win.projectList_);
    beat();
  }

  // A follow-up {"op":"compare","mode":"none"} plan must CLEAR the split view:
  // canvas mode off (no divider, not read-only), toolbar combo back to None.
  void chatCompareNonePlanClearsSplit() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QImage img(64, 48, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    win.loadImageWithLayout(img, QJsonObject());
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    const auto plan = [](const char* json) {
      return QJsonDocument(QJsonObject{
                 {"message", QJsonObject{{"content", QString::fromUtf8(json)}}}})
          .toJson(QJsonDocument::Compact);
    };
    // Turn 1 mirrors the reported flow: a red blank + a rectangle + the split
    // view in ONE plan (the layout makes the correction/refinement rounds run;
    // their empty responses are harmless keeps).
    mock.queue.append(plan(
        "{\"version\":1,\"reply\":\"split\",\"actions\":["
        "{\"op\":\"blank\",\"color\":\"red\",\"format\":\"a4\"},"
        "{\"op\":\"layout\",\"lines\":[{\"points\":[{\"x\":100,\"y\":100},"
        "{\"x\":400,\"y\":100},{\"x\":400,\"y\":300},{\"x\":100,\"y\":300},"
        "{\"x\":100,\"y\":100}],\"color\":\"#000000\"}]},"
        "{\"op\":\"compare\",\"mode\":\"vertical\",\"split\":0.4}]}"));
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    win.onChatSend("red album page with a rectangle, compared side by side");
    QTRY_COMPARE(win.canvas_->compareMode(), QStringLiteral("vertical"));
    mock.queue.clear();
    // The follow-up carries the mode alone: an echoed "split" beside "none" is a
    // parse failure since the registry's onlyWith rule (fixture 160).
    mock.response = plan("{\"version\":1,\"reply\":\"cleared\",\"actions\":["
                         "{\"op\":\"compare\",\"mode\":\"none\"}]}");
    win.onChatSend("turn the comparison off");
    QTRY_COMPARE(win.canvas_->compareMode(), QStringLiteral("none"));
    QVERIFY2(!win.canvas_->compareReadOnly(), "compare 'none' left the canvas read-only");
    QCOMPARE(win.compareCombo_->currentData().toString(), QStringLiteral("none"));
    beat();
  }

  // The card menu pops from a NESTED event loop, so the transcript can change
  // under it: a turn can land and repaint rows, the chat can be mid-close, and
  // the card itself can be deleted while the menu is up. Each of those crashed
  // (SIGSEGV inside QMenu::exec → QCocoaWindow::setVisible) or would use freed
  // memory after exec() returned.
  void chatCardMenuSurvivesTranscriptChurn() {
    MainWindow win(nullptr, false);
    win.resize(1150, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content",
                                 "{\"version\":1,\"reply\":\"working on it\",\"actions\":[]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    auto* dock = win.chatDock_;

    const auto cardWithBody = [&](const QString& body) -> QFrame* {
      for (QLabel* l : dock->findChildren<QLabel*>())
        if (l->property("chatBody").toString() == body)
          return qobject_cast<QFrame*>(l->parentWidget());
      return nullptr;
    };
    // Right-click a card's LABEL (the path in the crash report) and, from inside
    // the menu's event loop, run `duringMenu` before closing it.
    const auto popMenu = [&](QWidget* on, std::function<void()> duringMenu) {
      QTimer::singleShot(0, [duringMenu] {
        for (int i = 0; i < 100; ++i) {
          if (auto* m = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
            if (duringMenu) duringMenu();
            m->close();
            return;
          }
          QTest::qWait(5);
        }
      });
      const QPoint p = on->rect().center();
      QContextMenuEvent ev(QContextMenuEvent::Mouse, p, on->mapToGlobal(p));
      QApplication::sendEvent(on, &ev);
      settle([] { return QApplication::activePopupWidget() == nullptr; }, 40);
    };

    // (a) a turn IN FLIGHT, with the transcript repainting under the menu.
    dock->addAttachmentImage(QImage(8, 8, QImage::Format_RGB32), "x.png");
    win.onChatSend(QStringLiteral("crop it, make it b&w, and highlight the edges"));
    QTRY_VERIFY(!dock->isBusy());
    QFrame* userCard = cardWithBody(QStringLiteral("crop it, make it b&w, and highlight the edges"));
    QVERIFY(userCard);
    QLabel* body = nullptr;
    for (QLabel* l : userCard->findChildren<QLabel*>())
      if (!l->property("chatBody").toString().isEmpty()) body = l;
    QVERIFY(body);
    popMenu(body, [&] {
      // …the turn's tail landing while the menu is up.
      dock->appendAssistant(QStringLiteral("late note while the menu is open"));
      dock->appendLateNote(QStringLiteral("layout corrected"));
    });

    // (b) mid-close: the dock is still visible for the length of its slide, but
    // the menu must not pop into a surface that is about to be hidden.
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    win.actChat_->setChecked(false);
    QVERIFY2(dock->isVisible(), "the close should still be animating");
    {
      bool popped = false;
      QTimer::singleShot(0, [&popped] {
        if (auto* m = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
          popped = true;
          m->close();
        }
      });
      const QPoint p = body->rect().center();
      QContextMenuEvent ev(QContextMenuEvent::Mouse, p, body->mapToGlobal(p));
      QApplication::sendEvent(body, &ev);
      QTest::qWait(40);
      QVERIFY2(!popped, "the menu popped out of a chat that was closing");
    }
    if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim);
    QTRY_VERIFY(!dock->isVisible());
    win.actChat_->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    awaitAnim(win.chatAnim_);   // the open slide, on its own end

    // (c) the card is DELETED while its own menu is up — nothing may touch it
    // after exec() returns.
    dock->appendUser(QStringLiteral("doomed row"));
    QTRY_VERIFY(cardWithBody(QStringLiteral("doomed row")));
    QFrame* doomed = cardWithBody(QStringLiteral("doomed row"));
    QVERIFY(doomed);
    QTRY_VERIFY(doomed->isVisible());
    QLabel* doomedBody = nullptr;
    for (QLabel* l : doomed->findChildren<QLabel*>())
      if (!l->property("chatBody").toString().isEmpty()) doomedBody = l;
    QVERIFY(doomedBody);
    QPointer<QFrame> gone(doomed);
    popMenu(doomedBody, [&] {
      delete gone.data();   // the transcript settling mid-menu, at its worst
    });
    QVERIFY2(!gone, "the card should be gone");
    QTRY_VERIFY2(!dock->isBusy(), "the dock survived the churn");

    // …and a right-click on the now-dangling label's siblings still does nothing bad.
    QFrame* survivor = cardWithBody(QStringLiteral("late note while the menu is open"));
    if (survivor) popMenu(survivor, nullptr);
    win.llmClient_.reset();
    beat();
  }

  // A row the transcript is CLIPPING still has a reachable "…": the button is
  // parked against the intersection of the card and the viewport, not against the
  // card's own bottom (which is off screen for a half-shown row — the reported
  // bug: a reply cut off mid-sentence with no menu anywhere). Checked for a row
  // clipped at the TOP and one clipped at the BOTTOM, in the docked shape, the
  // floating one, and the context-menu panel.
  void chatRowMenuStaysInsideTheViewport() {
    MainWindow win(nullptr, false);
    win.resize(1100, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    // Enough long messages that the transcript really scrolls. Bubbles now
    // stretch to the full cap width once they need to wrap (browser
    // shrink-to-fit parity, applyChatBubbleWidths) rather than Qt's narrower
    // "balanced" wrap, so each one is shorter than it used to be — more
    // turns are needed to still leave a row clipped past the viewport edge.
    for (int i = 0; i < 14; ++i) {
      win.chatDock_->appendUser(
          QStringLiteral("Loading the image into incognito, converting to black & white "
                         "and cropping to portrait 3:4. Once it's done I will report "
                         "back with the result (%1).").arg(i));
      win.chatDock_->appendAssistant(
          QStringLiteral("Working on it — this reply is deliberately long so the row is "
                         "taller than a line and gets clipped by the viewport edge "
                         "while scrolling (%1).").arg(i));
    }
    settleLayout(win.chatDock_, 300);

    const auto globalRect = [](QWidget* w) {
      return QRect(w->mapToGlobal(QPoint(0, 0)), w->size());
    };
    const auto moreOf = [](QFrame* card) {
      return qobject_cast<QToolButton*>(card->property("chatMoreBtn").value<QObject*>());
    };
    // Hover the card the way the user does, then read where its "…" landed.
    const auto hover = [](QFrame* card) {
      QEvent enter(QEvent::Enter);
      QApplication::sendEvent(card, &enter);
    };
    // The jump pills' current box. A row whose "…" would land under them hides it
    // instead (placeChatCardMore's shift-else-hide — the pills are the higher-priority
    // control, checked by chatJumpPillsYieldToTheRowMenu), so the checks below that
    // want a SHOWN "…" must not pick a row sitting in that corner.
    const auto pillsBox = [&] {
      return static_cast<stencil::gui::ChatDock*>(win.chatDock_)->jumpPillsGlobalRect();
    };
    const auto crowdedByPills = [&](const QRect& cardGlobal) {
      const QRect p = pillsBox();
      return p.isValid() && cardGlobal.intersects(p.adjusted(-8, -8, 8, 8));
    };
    // Every surface is checked the same way: park the scroll somewhere in the
    // middle, then take a row clipped at each edge.
    const auto checkSurface = [&](QWidget* host, QScrollArea* scroll, const char* what) {
      QVERIFY2(scroll, what);
      QScrollBar* bar = scroll->verticalScrollBar();
      QVERIFY2(bar->maximum() > 0, qPrintable(QString("%1: the transcript does not scroll")
                                                  .arg(what)));
      // Read LIVE: the transcript's width settles over the first scroll steps (301 -> 334).
      const auto vpNow = [&] { return globalRect(scroll->viewport()); };
      QFrame* clippedTop = nullptr;
      QFrame* clippedBottom = nullptr;
      // A slice tall enough to CARRY the pill (a shorter one deliberately hides
      // it — that rule has its own checks below).
      const int room = 21 + 8;
      // Bubbles that all wrap to the same line count land in exact lockstep, so a card
      // pitch that divides the viewport evenly can leave no row both clipped and clear of
      // the jump pills. Walk out from the middle until BOTH clipped rows show their "…".
      for (int v = bar->maximum() / 2; v <= bar->maximum(); v += 12) {
        bar->setValue(v);
        QTest::qWait(30);
        clippedTop = clippedBottom = nullptr;
        for (QFrame* card : host->findChildren<QFrame*>()) {
          if (!card->property("chatMoreBtn").isValid()) continue;
          const QRect g = globalRect(card);
          if (g.intersected(vpNow()).height() < room) continue;
          if (g.top() < vpNow().top()) clippedTop = card;
          if (g.bottom() > vpNow().bottom()) clippedBottom = card;
        }
        if (!clippedTop || !clippedBottom) continue;
        bool bothShow = true;
        for (QFrame* card : {clippedTop, clippedBottom}) {
          hover(card);
          QToolButton* more = moreOf(card);
          if (!more || !more->isVisible()) { bothShow = false; break; }
        }
        if (bothShow) break;
      }
      QVERIFY2(clippedTop, qPrintable(QString("%1: no row clipped at the top").arg(what)));
      QVERIFY2(clippedBottom, qPrintable(QString("%1: no row clipped at the bottom").arg(what)));
      for (QFrame* card : {clippedTop, clippedBottom}) {
        hover(card);
        QToolButton* more = moreOf(card);
        QVERIFY2(more, qPrintable(QString("%1: a clipped row has no \"…\"").arg(what)));
        QVERIFY2(more->isVisible(),
                 qPrintable(QString("%1: the clipped row's \"…\" never showed").arg(what)));
        QVERIFY2(vpNow().contains(globalRect(more)),
                 qPrintable(QString("%1: the \"…\" sits outside the viewport (%2 vs %3)")
                                .arg(what)
                                .arg(QDebug::toString(globalRect(more)))
                                .arg(QDebug::toString(vpNow()))));
      }
      // …and it tracks the view: scrolling must not leave it behind.
      hover(clippedBottom);
      bar->setValue(bar->value() + 40);
      QTest::qWait(80);
      QToolButton* more = moreOf(clippedBottom);
      if (more->isVisible())
        QVERIFY2(globalRect(scroll->viewport()).contains(globalRect(more)),
                 qPrintable(QString("%1: the \"…\" fell out of the viewport on scroll").arg(what)));
    };

    // NARROW transcript: the bubbles reach the edge, which is where the "…"
    // (it hangs OUTSIDE the bubble) was landing half over the boundary.
    const auto checkNarrow = [&](QWidget* host, QScrollArea* scroll, const char* what) {
      QScrollBar* bar = scroll->verticalScrollBar();
      // This test is about the HORIZONTAL edge (a narrow column's pill hanging off
      // the bubble's side), not vertical clipping — so the row must be FULLY on
      // screen AND clear of the jump pills, each of which legitimately hides the
      // "…" under its own rule, checked elsewhere. A narrow column fits about one
      // row at a time, so each kind is hunted — and checked — at its own scroll
      // position rather than whichever rows happen to share the current one.
      for (const char* kind : {"chatCardUser",         // its "…" hangs off the LEFT
                               "chatCardAssistant"}) { // …and this one's off the RIGHT
        QFrame* card = nullptr;
        for (int v = 0; v <= bar->maximum() && !card; v += 12) {
          bar->setValue(v);
          QTest::qWait(20);
          const QRect seen = globalRect(scroll->viewport());
          for (QFrame* c : host->findChildren<QFrame*>()) {
            if (!c->property("chatMoreBtn").isValid()) continue;
            if (c->objectName() != QLatin1String(kind)) continue;
            const QRect g = globalRect(c);
            if (!seen.contains(g) || crowdedByPills(g)) continue;
            card = c;
            break;
          }
        }
        QVERIFY2(card, qPrintable(QString("%1: no fully visible %2 row to hang a \"…\" off")
                                      .arg(what, kind)));
        const QRect vp = globalRect(scroll->viewport());
        hover(card);
        QToolButton* more = moreOf(card);
        QVERIFY(more && more->isVisible());
        const QRect r = globalRect(more);
        QVERIFY2(vp.contains(r),
                 qPrintable(QString("%1 (%2): the \"…\" is clipped by the edge (%3 vs %4)")
                                .arg(what, card->objectName(),
                                     QDebug::toString(r), QDebug::toString(vp))));
        QVERIFY2(r.left() >= vp.left() + 4 && r.right() <= vp.right() - 4,
                 qPrintable(QString("%1 (%2): no padding at the edge (%3 in %4)")
                                .arg(what, card->objectName(),
                                     QDebug::toString(r), QDebug::toString(vp))));
        // …and inside the widget that actually CLIPS it (the scrolled content),
        // not merely inside the viewport.
        QVERIFY2(globalRect(scroll->widget()).contains(r),
                 qPrintable(QString("%1 (%2): the pill hangs outside its clipping parent")
                                .arg(what, card->objectName())));
        // It is CHROME, not a transcript row: the edge-reveal must not dissolve
        // it (it is pinned at the edge by design) nor replace its accent glow.
        QVERIFY2(qobject_cast<QGraphicsDropShadowEffect*>(more->graphicsEffect()),
                 qPrintable(QString("%1 (%2): the \"…\" lost its glow to the edge reveal")
                                .arg(what, card->objectName())));
      }
    };

    // A row whose visible SLICE is too short to hold the pill does not show one:
    // the clamp would park it across the neighbouring card, which reads as a bug.
    // A fully visible row clear of the jump pills always shows it.
    const auto checkSliver = [&](QWidget* host, QScrollArea* scroll, const char* what) {
      QScrollBar* bar = scroll->verticalScrollBar();
      const QRect vp = globalRect(scroll->viewport());
      // Walk the scroll until some row is only a sliver at the viewport's edge.
      QFrame* sliver = nullptr;
      QFrame* whole = nullptr;
      for (int v = 0; v <= bar->maximum() && !sliver; v += 7) {
        bar->setValue(v);
        QTest::qWait(20);
        whole = nullptr;   // only a row fully visible at THIS position counts
        for (QFrame* card : host->findChildren<QFrame*>()) {
          if (!card->property("chatMoreBtn").isValid()) continue;
          const QRect g = globalRect(card);
          const int slice = g.intersected(vp).height();
          if (slice > 2 && slice < 16 && g.height() > 40) sliver = card;
          if (vp.contains(g) && !crowdedByPills(g)) whole = card;
        }
      }
      QVERIFY2(sliver, qPrintable(QString("%1: no row ended up a sliver").arg(what)));
      hover(sliver);
      QToolButton* more = moreOf(sliver);
      QVERIFY(more);
      QVERIFY2(!more->isVisible(),
               qPrintable(QString("%1: a sliver of a row still shows its \"…\"").arg(what)));
      // …and it must not be straddling anything if it somehow shows later.
      if (whole) {
        hover(whole);
        QToolButton* m2 = moreOf(whole);
        QVERIFY2(m2 && m2->isVisible(),
                 qPrintable(QString("%1: a fully visible row lost its \"…\"").arg(what)));
        const QRect r = globalRect(m2);
        for (QFrame* card : host->findChildren<QFrame*>()) {
          if (card == whole || !card->property("chatMoreBtn").isValid()) continue;
          if (!card->isVisible()) continue;
          QVERIFY2(!globalRect(card).intersects(r),
                   qPrintable(QString("%1: the \"…\" overlaps a neighbouring card").arg(what)));
        }
      }
    };

    QScrollArea* dockScroll = nullptr;
    for (QScrollArea* a : win.chatDock_->findChildren<QScrollArea*>()) dockScroll = a;
    checkSurface(win.chatDock_, dockScroll, "docked");
    checkSliver(win.chatDock_, dockScroll, "docked");
    win.chatDock_->setMinimumWidth(0);
    win.resizeDocks({win.chatDock_}, {230}, Qt::Horizontal);   // squeeze it
    settleLayout(win.chatDock_, 300);
    checkNarrow(win.chatDock_, dockScroll, "docked narrow");

    // The floating/compact shape uses the same transcript widget.
    win.chatDock_->setFloating(true);
    win.chatDock_->resize(360, 460);
    settleLayout(win.chatDock_, 300);
    checkSurface(win.chatDock_, dockScroll, "floating");
    win.chatDock_->resize(240, 460);   // narrow float: bubbles at both edges
    settleLayout(win.chatDock_, 300);
    checkNarrow(win.chatDock_, dockScroll, "floating narrow");
    win.chatDock_->setFloating(false);
    settleLayout(win.chatDock_, 200);

    // …and so does the context menu's panel.
    win.ensureChatMenuPanel();
    QVERIFY(win.chatMenuPanel_);
    // It normally lives inside the menu's QWidgetAction; show it in place so it
    // lays out (a hidden scroll area has no range to scroll).
    win.chatMenuPanel_->setGeometry(20, 20, 340, 640);
    win.chatMenuPanel_->show();
    // The panel is built lazily and mirrors the SHARED history, which these
    // direct dock appends never touched — mirror the same volume into it.
    for (int i = 0; i < 14; ++i) {
      win.chatMirror(QStringLiteral("You"),
                     QStringLiteral("Loading the image into incognito, converting to "
                                    "black & white and cropping to portrait 3:4 (%1).").arg(i),
                     false);
      win.chatMirror(QStringLiteral("Assistant"),
                     QStringLiteral("Working on it — this reply is deliberately long so "
                                    "the row is taller than a line and gets clipped by "
                                    "the viewport edge while scrolling (%1).").arg(i),
                     false);
    }
    settleLayout(win.chatMenuPanel_, 200);
    checkSurface(win.chatMenuPanel_,
                 win.chatMenuPanel_->findChild<QScrollArea*>("chatMenuTranscript"), "menu panel");
    beat();
  }

  // The transcript's jump pills float in the bottom-right corner — exactly where a
  // clipped row's "…" lands, and two round controls stacked are unclickable. The pills
  // are the higher-priority control: they stay, and the row trigger shifts clear, or
  // hides when a bubble too short leaves nowhere to shift it to (browser parity).
  void chatJumpPillsYieldToTheRowMenu() {
    MainWindow win(nullptr, false);
    win.resize(1100, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    for (int i = 0; i < 10; ++i)
      win.chatDock_->appendAssistant(
          QStringLiteral("Row %1 — long enough that the transcript scrolls and a row "
                         "gets clipped at the bottom edge of the viewport.").arg(i));
    // A NARROW dock: the bubbles then reach the width cap, so an assistant row's
    // "…" (it hangs off the right) lands in the pills' corner.
    win.chatDock_->setMinimumWidth(0);
    win.resizeDocks({win.chatDock_}, {250}, Qt::Horizontal);
    QScrollArea* scroll = nullptr;
    for (QScrollArea* a : win.chatDock_->findChildren<QScrollArea*>()) scroll = a;
    QVERIFY(scroll);
    QScrollBar* bar = scroll->verticalScrollBar();
    QTRY_VERIFY(bar->maximum() > 24);
    bar->setValue(bar->maximum() / 2);   // mid-log: both pills want to show
    settleLayout(win.chatDock_, 200);
    const auto jumps = win.chatDock_->findChildren<QToolButton*>(QStringLiteral("chatJumpBtn"));
    QCOMPARE(jumps.size(), 2);
    // Precondition: no row menu on screen, so the pills' own rule lets them show
    // (a scroll can leave one from an earlier hover, and they yield to it). The
    // clear + nudge is re-run each poll, since only a scroll re-evaluates it.
    const auto pillsUp = [&] {
      for (QToolButton* m : win.chatDock_->findChildren<QToolButton*>("chatCardMore"))
        m->hide();
      // Re-centre each poll: the bubble-width pass keeps changing the range while
      // the transcript settles, and an end position legitimately hides one pill.
      bar->setValue(bar->maximum() / 2);
      bar->setValue(bar->value() + 1);
      bar->setValue(bar->value() - 1);
      return jumps[0]->isVisible() && jumps[1]->isVisible();
    };
    QTRY_VERIFY(pillsUp());
    const auto pillsRect = [&] {
      return QRect(jumps[0]->mapToGlobal(QPoint(0, 0)), jumps[0]->size())
          .united(QRect(jumps[1]->mapToGlobal(QPoint(0, 0)), jumps[1]->size()));
    };

    // Hover the row whose "…" lands in the pills' corner — same real Enter path a
    // cursor takes, so placeChatCardMore runs its actual shift/hide logic.
    QFrame* card = nullptr;
    for (QFrame* f : win.chatDock_->findChildren<QFrame*>())
      if (f->property("chatMoreBtn").isValid() &&
          QRect(f->mapToGlobal(QPoint(0, 0)), f->size())
              .intersects(QRect(scroll->viewport()->mapToGlobal(QPoint(0, 0)),
                                scroll->viewport()->size())))
        card = f;
    QVERIFY(card);
    QEvent enter(QEvent::Enter);
    QApplication::sendEvent(card, &enter);
    auto* more = qobject_cast<QToolButton*>(card->property("chatMoreBtn").value<QObject*>());
    QVERIFY(more);
    // The pills never stand down for this any more — up before AND after the hover.
    QVERIFY2(jumps[0]->isVisible() && jumps[1]->isVisible(),
             "the pills should still be up before the button reaches them");
    QVERIFY2(jumps[0]->isVisible() && jumps[1]->isVisible(),
             "the jump pills must stay up — the row's trigger yields, not them");
    // The trigger itself either shifted clear of the pills, or — nowhere left in
    // this row's own visible slice to shift it to — hid instead. Either way it
    // must never simply sit ON them (unclickable, two round controls stacked).
    if (more->isVisible()) {
      QVERIFY2(!QRect(more->mapToGlobal(QPoint(0, 0)), more->size()).intersects(pillsRect()),
               "the trigger sat under the pills instead of shifting clear of them");
    }
    // …and forcing it directly onto the pills (bypassing the real placement path,
    // the way a stale position from before a resize might) is corrected on the next
    // real placement pass, never by the pills hiding.
    if (more->isVisible()) {
      more->move(more->parentWidget()->mapFromGlobal(pillsRect().topLeft()));
      win.chatDock_->revalidateMoreButtons();
      QVERIFY2(jumps[0]->isVisible() && jumps[1]->isVisible(), "the pills stayed up");
      if (more->isVisible())
        QVERIFY2(!QRect(more->mapToGlobal(QPoint(0, 0)), more->size()).intersects(pillsRect()),
                 "revalidateMoreButtons must pull the trigger back off the pills");
    }
    beat();
  }

  // Error and stopped cards are settled rows, so they carry the SAME affordances
  // the browser gives them: the hover "…", the right-click menu (Copy message /
  // Insert into prompt) and the neutral Resend control. Desktop
  // offered none of it — the stopped card was built outside appendCard entirely.
  void chatErrorCardsCarryTheRowMenu() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    win.chatHistory_.append({QStringLiteral("user"), QStringLiteral("remove this project"), {}});
    // The panel exists BEFORE the failure, as it does in use — errors are
    // mirrored live (they never enter the replayed history).
    win.ensureChatMenuPanel();
    win.chatError(QStringLiteral("Could not read the assistant's plan: \"clear\" is an "
                                 "editor-settings op"),
                  QString());
    // …and a stopped turn, which is built through the PENDING card on both
    // surfaces, not through the ordinary append path.
    win.chatDock_->showPending();
    win.chatMirrorPending(true);
    win.chatDock_->markPendingStopped(QStringLiteral("remove this project"));
    win.chatMirrorStopped(QStringLiteral("remove this project"));
    settleLayout(win.chatDock_, 200);

    const auto menuItems = [](QWidget* w) {
      QStringList names;
      QTimer::singleShot(0, [&names] {
        for (int i = 0; i < 100; ++i) {
          if (auto* m = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
            for (QAction* a : m->actions()) names << a->text();
            m->close();
            return;
          }
          QTest::qWait(5);
        }
      });
      const QPoint pos = w->rect().center();
      QContextMenuEvent ev(QContextMenuEvent::Mouse, pos, w->mapToGlobal(pos));
      QApplication::sendEvent(w, &ev);
      settle([&names] { return !names.isEmpty(); }, 20);
      return names;
    };

    QList<QFrame*> errorCards = win.chatDock_->findChildren<QFrame*>("chatCardError");
    QVERIFY2(errorCards.size() >= 2, "expected the error card AND the stopped card");
    for (QFrame* card : errorCards) {
      const QString what = card->findChildren<QLabel*>().isEmpty()
                               ? QString()
                               : card->findChildren<QLabel*>().first()->text().left(20);
      QVERIFY2(card->property("chatMoreBtn").value<QObject*>(),
               qPrintable(QString("%1: no \"…\" on an error card").arg(what)));
      QCOMPARE(card->contextMenuPolicy(), Qt::CustomContextMenu);
      const QStringList items = menuItems(card);
      QVERIFY2(items.contains(QStringLiteral("Copy message")), qPrintable(what + ": no Copy"));
      QVERIFY2(items.contains(QStringLiteral("Insert into prompt")),
               qPrintable(what + ": no Insert into prompt"));
      // Resend is a USER-bubble item (browser parity); the error row's own
      // retry control is the neutral refresh button instead.
      QVERIFY2(!items.contains(QStringLiteral("Resend")),
               qPrintable(what + ": Resend leaked onto a non-user row"));
      QVERIFY2(!items.contains(QStringLiteral("Select all")),
               qPrintable(what + ": Select all is still offered"));
      QVERIFY2(card->findChild<QToolButton*>("chatRetry"),
               qPrintable(what + ": the error card has no Resend control"));
    }

    // The mirrored panel gets the same treatment (the browser's flyout does).
    // The panel's cards are only on screen while its menu is open — show it, or
    // the row menu rightly refuses to pop into an invisible surface.
    win.chatMenuPanel_->setGeometry(20, 20, 340, 620);
    win.chatMenuPanel_->show();
    settleLayout(win.chatMenuPanel_, 150);
    QList<QFrame*> panelErrors = win.chatMenuPanel_->findChildren<QFrame*>("chatCardError");
    QVERIFY2(!panelErrors.isEmpty(), "the panel mirrored no error card");
    bool sawRetry = false;
    for (QFrame* card : panelErrors) {
      QVERIFY2(card->property("chatMoreBtn").value<QObject*>(),
               "no \"…\" on the panel's error card");
      const QStringList items = menuItems(card);
      QVERIFY(items.contains(QStringLiteral("Copy message")));
      QVERIFY(items.contains(QStringLiteral("Insert into prompt")));
      if (card->findChild<QToolButton*>("chatRetry")) sawRetry = true;
    }
    QVERIFY2(sawRetry, "the panel's error card offers no Resend");
    beat();
  }

  // Right-click on a transcript bubble → Copy message / Insert into prompt on
  // every card (user, assistant, note), plus Resend on user bubbles —
  // the same turn again, original attachments included. Also pins that a mouse
  // selection inside a bubble can be copied with the Copy shortcut.
  void chatBubbleContextMenu() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content",
                                 "{\"version\":1,\"reply\":\"hi there\",\"actions\":[]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    win.actChat_->setChecked(true);   // the menu only pops on a VISIBLE surface
    QTRY_VERIFY(dock->isVisible());
    awaitAnim(win.chatAnim_);   // the open slide, on its own end
    QImage att(24, 24, QImage::Format_RGB32);
    att.fill(Qt::green);
    dock->addAttachmentImage(att, "cat.png");
    win.onChatSend("highlight the cat");
    QTRY_VERIFY(!dock->isBusy());
    const int firstImages = mock.body.value("messages").toArray().last().toObject()
                                .value("images").toArray().size();
    QVERIFY2(firstImages >= 1, "the first turn did not carry the attachment");
    const int postsBefore = mock.allBodies.size();

    // Open the card's custom context menu and activate the item named `text`
    // (keyboard activation, so the blocking exec() returns that action).
    const auto pickMenuItem = [](QWidget* w, const QString& text) {
      bool found = false;
      QTimer::singleShot(0, [text, &found] {
        for (int i = 0; i < 100; ++i) {
          if (auto* m = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
            for (QAction* a : m->actions())
              if (a->text() == text) {
                found = true;
                m->setActiveAction(a);
                QTest::keyClick(m, Qt::Key_Return);
                return;
              }
            m->close();
            return;
          }
          QTest::qWait(5);
        }
      });
      const QPoint pos = w->rect().center();
      QContextMenuEvent ev(QContextMenuEvent::Mouse, pos, w->mapToGlobal(pos));
      QApplication::sendEvent(w, &ev);
      settle([&found] { return found; }, 20);
      return found;
    };

    QFrame* userCard = nullptr;
    for (QFrame* f : dock->findChildren<QFrame*>("chatCardUser")) userCard = f;
    QVERIFY2(userCard, "no user bubble in the transcript");
    QFrame* assistantCard = nullptr;
    QLabel* userBody = nullptr;
    for (QLabel* l : dock->findChildren<QLabel*>()) {
      if (l->property("chatBody").toString() == QLatin1String("hi there"))
        assistantCard = qobject_cast<QFrame*>(l->parentWidget());
      if (l->property("chatBody").toString() == QLatin1String("highlight the cat"))
        userBody = l;
    }
    QVERIFY2(assistantCard, "no assistant bubble in the transcript");
    QVERIFY2(userBody, "no body label on the user bubble");

    // Copy message: the plain, role-stripped text lands on the clipboard.
    QGuiApplication::clipboard()->setText(QString());
    QVERIFY(pickMenuItem(userCard, QStringLiteral("Copy message")));
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("highlight the cat"));
    QVERIFY(pickMenuItem(assistantCard, QStringLiteral("Copy message")));
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("hi there"));
    // …and a note card gets the same menu.
    dock->appendNote(QStringLiteral("just a note"));
    QFrame* noteCard = nullptr;
    for (QFrame* f : dock->findChildren<QFrame*>("chatCardMuted")) noteCard = f;
    QVERIFY2(noteCard, "no note card in the transcript");
    QVERIFY(pickMenuItem(noteCard, QStringLiteral("Copy message")));
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("just a note"));
    // No Resend anywhere but on the user's own bubbles.
    QVERIFY(!pickMenuItem(assistantCard, QStringLiteral("Resend")));

    // "Select all" is NOT offered (dropped from both surfaces — a drag selects
    // what you actually want, and Copy message already takes the whole row).
    QVERIFY2(!pickMenuItem(userCard, QStringLiteral("Select all")),
             "Select all is still in the row menu");
    // What it stood in for still works: a selection inside the bubble copies
    // with the shortcut, because the label takes focus on click.
    userBody->setSelection(0, static_cast<int>(userBody->text().size()));
    userBody->setFocus(Qt::OtherFocusReason);
    QCOMPARE(userBody->selectedText(), QStringLiteral("highlight the cat"));
    // The offscreen platform leaves no window active after the popup closes, so
    // assert the WINDOW's recorded focus widget (what activation restores).
    QTRY_COMPARE(userBody->window()->focusWidget(), static_cast<QWidget*>(userBody));
    QGuiApplication::clipboard()->setText(QString());
    QTest::keyClick(userBody, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("highlight the cat"));

    // Insert into prompt: the text lands in the composer, which takes focus.
    auto* input = dock->findChild<QPlainTextEdit*>();
    QVERIFY(input);
    input->clear();
    QVERIFY(pickMenuItem(assistantCard, QStringLiteral("Insert into prompt")));
    QCOMPARE(input->toPlainText(), QStringLiteral("hi there"));
    input->clear();

    // Resend: a SECOND request with the same text AND the original turn's
    // attachment back in the payload (the tray drained on the first send).
    QVERIFY(pickMenuItem(userCard, QStringLiteral("Resend")));
    QTRY_VERIFY(!dock->isBusy());
    QCOMPARE(mock.allBodies.size(), postsBefore + 1);
    const QJsonObject last = mock.body.value("messages").toArray().last().toObject();
    QCOMPARE(last.value("content").toString(), QStringLiteral("highlight the cat"));
    QCOMPARE(last.value("images").toArray().size(), firstImages);
    QCOMPARE(dock->attachedImages().size(), 0);   // the resend drained the tray too
    beat();
  }

  // Escape closes the chat card menu natively: exec() returns no action, the
  // popup grab is released, nothing runs — guarded so the menu reveal
  // animation can never break it.
  void chatCardMenuEscapeCloses() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    win.actChat_->setChecked(true);   // the menu only pops on a VISIBLE surface
    QTRY_VERIFY(dock->isVisible());
    awaitAnim(win.chatAnim_);   // the open slide, on its own end
    dock->appendNote(QStringLiteral("escape me"));
    QFrame* noteCard = nullptr;
    for (QFrame* f : dock->findChildren<QFrame*>("chatCardMuted")) noteCard = f;
    QVERIFY2(noteCard, "no note card in the transcript");

    bool sawMenu = false;
    QGuiApplication::clipboard()->setText(QStringLiteral("sentinel"));
    QTimer::singleShot(0, [&sawMenu] {
      if (!QTest::qWaitFor(
              [] { return QApplication::activePopupWidget() != nullptr; }, 500))
        return;
      auto* m = qobject_cast<QMenu*>(QApplication::activePopupWidget());
      if (!m) return;
      sawMenu = true;
      QTest::keyClick(m, Qt::Key_Escape);
    });
    const QPoint pos = noteCard->rect().center();
    QContextMenuEvent ev(QContextMenuEvent::Mouse, pos, noteCard->mapToGlobal(pos));
    QApplication::sendEvent(noteCard, &ev);   // blocks in exec() until Escape lands
    QTRY_VERIFY2(sawMenu, "the card menu never opened");
    QTRY_VERIFY(QApplication::activePopupWidget() == nullptr);   // grab released
    // exec() returned nullptr: no action ran, the sentinel clipboard survives.
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("sentinel"));
    beat();
  }

  // Hovering a bubble reveals the ghost "⋯" in its bottom corner — LEFT on the
  // right-aligned user bubbles, RIGHT on assistant cards — and clicking it opens
  // the same menu as a right-click. Leave hides it again.
  void chatCardHoverMenuButton() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content",
                                 "{\"version\":1,\"reply\":\"hi there\",\"actions\":[]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    dock->show();   // visibility checks below need visible ancestors
    win.onChatSend("highlight the cat");
    QTRY_VERIFY(!dock->isBusy());
    settleLayout(dock, 20);   // let the transcript layout activate (shows the cards)

    QFrame* userCard = nullptr;
    for (QFrame* f : dock->findChildren<QFrame*>("chatCardUser")) userCard = f;
    QVERIFY2(userCard, "no user bubble in the transcript");
    QFrame* assistantCard = nullptr;
    for (QLabel* l : dock->findChildren<QLabel*>())
      if (l->property("chatBody").toString() == QLatin1String("hi there"))
        assistantCard = qobject_cast<QFrame*>(l->parentWidget());
    QVERIFY2(assistantCard, "no assistant bubble in the transcript");

    // Offscreen has no real cursor, so hover is a synthetic Enter event.
    const auto hover = [](QWidget* w) {
      QEnterEvent ev(QPointF(2, 2), QPointF(2, 2), w->mapToGlobal(QPoint(2, 2)));
      QApplication::sendEvent(w, &ev);
    };
    // The button reparents beside its card on the first place — resolve it via
    // the property link, not parentage.
    auto* userMore = qobject_cast<QToolButton*>(
        userCard->property("chatMoreBtn").value<QObject*>());
    auto* asstMore = qobject_cast<QToolButton*>(
        assistantCard->property("chatMoreBtn").value<QObject*>());
    QVERIFY2(userMore && asstMore, "cards are missing the hover menu button");
    QVERIFY(userMore->toolTip().isEmpty());   // no "Message actions" tooltip
    QVERIFY(!userMore->isVisible());   // hidden at rest
    hover(userCard);
    QVERIFY(userMore->isVisible());
    // BESIDE the user bubble on its left — never overlapping it (parent coords
    // after placeCardMore reparents the button next to the card).
    QVERIFY(userMore->geometry().right() < userCard->geometry().left());
    QVERIFY(qAbs(userMore->geometry().bottom() - userCard->geometry().bottom()) <= 2);
    hover(assistantCard);
    QVERIFY(asstMore->isVisible());
    // …and beside the assistant bubble on its right.
    QVERIFY(asstMore->geometry().left() > assistantCard->geometry().right());
    QVERIFY(qAbs(asstMore->geometry().bottom() - assistantCard->geometry().bottom()) <= 2);
    // Leave hides after a short grace (the button sits across a gap) — park the
    // offscreen cursor away from both widgets first so the check can pass.
    QCursor::setPos(win.mapToGlobal(QPoint(5, 5)));
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(assistantCard, &leave);
    QTRY_VERIFY(!asstMore->isVisible());   // gone when the cursor moves off

    // Clicking it opens the SAME card menu (timer-driven pick, as above).
    bool sawMenu = false;
    QGuiApplication::clipboard()->setText(QString());
    QTimer::singleShot(0, [&sawMenu] {
      // Bounded wait for the blocking exec()'s popup, then activate the item.
      if (!QTest::qWaitFor(
              [] { return QApplication::activePopupWidget() != nullptr; }, 500))
        return;
      auto* m = qobject_cast<QMenu*>(QApplication::activePopupWidget());
      if (!m) return;
      for (QAction* a : m->actions())
        if (a->text() == QLatin1String("Copy message")) {
          sawMenu = true;
          m->setActiveAction(a);
          QTest::keyClick(m, Qt::Key_Return);
          return;
        }
      m->close();
    });
    userMore->click();   // blocking until the menu picks/closes
    QTRY_VERIFY2(sawMenu, "the hover button did not open the card menu");
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("highlight the cat"));
    // The menu is gone, so a Leave hides the button again (after the grace).
    QApplication::sendEvent(userCard, &leave);
    QTRY_VERIFY(!userMore->isVisible());
    beat();
  }

  // Retry resends the failed turn WITH its attachments: the tray drained on the
  // first send, so the retry handler re-queues them — thumbnails on the resent
  // bubble, images back in the wire payload.
  void chatRetryResendsAttachments() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.status = 0;
    mock.netError = "network down";
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    QImage att(24, 24, QImage::Format_RGB32);
    att.fill(Qt::green);
    dock->addAttachmentImage(att, "cat.png");
    win.onChatSend("highlight the cat");
    QTRY_VERIFY(!dock->isBusy());
    QCOMPARE(dock->attachedImages().size(), 0);   // the send drained the tray
    const int firstImages = mock.body.value("messages").toArray().last().toObject()
                                .value("images").toArray().size();
    QVERIFY2(firstImages >= 1, "the failed turn carried the attachment");
    // The network heals; the error card's retry resends the whole turn.
    mock.status = 200;
    mock.netError.clear();
    mock.response = QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content",
                                 "{\"version\":1,\"reply\":\"ok\",\"actions\":[]}"}}}})
                       .toJson(QJsonDocument::Compact);
    QToolButton* retry = nullptr;
    for (QToolButton* b : dock->findChildren<QToolButton*>("chatRetry")) retry = b;
    QVERIFY2(retry, "no retry button on the failed turn");
    retry->click();
    QTRY_VERIFY(!dock->isBusy());
    const auto msgs = mock.body.value("messages").toArray();
    const int retryImages =
        msgs.last().toObject().value("images").toArray().size();
    QCOMPARE(retryImages, firstImages);   // the resend carries the image again
    QCOMPARE(dock->attachedImages().size(), 0);   // and drained normally after
    beat();
  }

  // A follow-up turn with NO new attachments (an ask-card answer) keeps the prior
  // turn's attachments: the editing plan it triggers still adopts that image on an
  // empty canvas (browser parity — turnAttachments only resets when new ones queue).
  void chatFollowUpAdoptsPriorAttachment() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    // Turn 1: attachment queued, but the model only asks back — nothing to run,
    // so nothing is adopted and the canvas stays empty.
    mock.response = QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content",
                                 "{\"version\":1,\"reply\":\"Which photo first?\",\"actions\":[]}"}}}})
                       .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    QImage att(64, 48, QImage::Format_RGB32);
    att.fill(Qt::darkMagenta);
    dock->addAttachmentImage(att, "cat.png");
    win.onChatSend("edit these photos");
    QTRY_VERIFY(!dock->isBusy());
    QVERIFY(!win.canvas_->hasImage());
    QCOMPARE(dock->attachedImages().size(), 0);   // the send drained the tray
    // Turn 2: the attachment-less answer triggers an editing plan — it must still
    // adopt turn 1's image instead of failing on the empty canvas.
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"Making it black and white.\",\"actions\":"
                      "[{\"op\":\"filter\",\"mode\":\"bw\"}]}"}}}})
                       .toJson(QJsonDocument::Compact);
    win.onChatSend("the first one");
    QTRY_VERIFY(win.canvas_->hasImage());
    beat();
  }

  // §2.1 multi-image plans: one turn edits SEVERAL attached images — each
  // `image` op switches the working image to that attachment, and each `save`
  // persists the result as its own LOCAL project (fresh id per save, named
  // after the image it was working on, suffixed when the name is taken).
  void chatMultiImagePlanSavesOneProjectPerImage() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    const auto wrap = [](const char* plan) {
      return QJsonDocument(QJsonObject{{"message", QJsonObject{{"content", plan}}}})
          .toJson(QJsonDocument::Compact);
    };
    mock.queue.append(wrap(
        "{\"version\":1,\"reply\":\"Both done.\",\"actions\":["
        "{\"op\":\"image\",\"index\":1},{\"op\":\"filter\",\"mode\":\"bw\"},{\"op\":\"save\"},"
        "{\"op\":\"image\",\"index\":2},{\"op\":\"filter\",\"mode\":\"sepia\"},"
        "{\"op\":\"save\",\"name\":\"second\"}]}"));
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    QImage shore(64, 48, QImage::Format_RGB32);
    shore.fill(Qt::darkCyan);
    QImage dunes(40, 30, QImage::Format_RGB32);
    dunes.fill(Qt::darkMagenta);
    dock->addAttachmentImage(shore, "shore.jpg");
    dock->addAttachmentImage(dunes, "dunes.png");
    // An unnamed save takes its attachment's name, and a TAKEN name suffixes — so a
    // leftover "shore" in the shared state dir would rename everything asserted below.
    QStringList stale;
    for (const auto& p : win.projectList_) {
      const QString name = QString::fromStdString(p.meta.name);
      if (name.startsWith(QStringLiteral("shore")) || name.startsWith(QStringLiteral("second")))
        stale << QString::fromStdString(p.meta.id);
    }
    for (const QString& id : stale) win.eraseLocalProject(id);
    const int before = int(win.projectList_.size());
    win.onChatSend("make the first b&w and the second sepia, then save both");
    QTRY_VERIFY(!dock->isBusy());
    // One project per image, in plan order: the unnamed save took the name of
    // the attachment it was working on, the named one kept its own.
    QTRY_COMPARE(int(win.projectList_.size()), before + 2);
    QCOMPARE(QString::fromStdString(win.projectList_.at(before).meta.name),
             QStringLiteral("shore"));
    QCOMPARE(QString::fromStdString(win.projectList_.at(before + 1).meta.name),
             QStringLiteral("second"));
    QVERIFY2(win.projectList_.at(before).meta.id != win.projectList_.at(before + 1).meta.id,
             "each save must promote to a FRESH project, never overwrite the last");
    // …and each one holds ITS image, not the last one processed.
    QImage firstSaved, secondSaved;
    QVERIFY(firstSaved.load(win.projectList_.at(before).imagePath));
    QVERIFY(secondSaved.load(win.projectList_.at(before + 1).imagePath));
    QCOMPARE(firstSaved.size(), shore.size());
    QCOMPARE(secondSaved.size(), dunes.size());
    // The LAST processed image stays in the editor, with its own filter.
    QVERIFY(win.canvas_->hasImage());
    QCOMPARE(win.settings_.imageFilter, QStringLiteral("sepia"));
    QCOMPARE(win.activeProjectName(), QStringLiteral("second"));

    // A follow-up turn saving image 1 again cannot reuse the taken name: it
    // suffixes instead of losing the save.
    mock.queue.append(wrap(
        "{\"version\":1,\"reply\":\"Saved again.\",\"actions\":["
        "{\"op\":\"image\",\"index\":1},{\"op\":\"save\"}]}"));
    win.onChatSend("save the first one again");
    QTRY_VERIFY(!dock->isBusy());
    QTRY_COMPARE(int(win.projectList_.size()), before + 3);
    QCOMPARE(QString::fromStdString(win.projectList_.at(before + 2).meta.name),
             QStringLiteral("shore 2"));
    beat();
  }

  // §3.0: a multi-image plan that also draws is still ONE round — and it must not
  // apologise for a pass that no longer exists (this used to append a "layout
  // correction skipped" note).
  void chatMultiImageLayoutStillOneRound() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"Outlined.\",\"actions\":["
                      "{\"op\":\"image\",\"index\":1},{\"op\":\"layout\",\"lines\":["
                      "{\"points\":[{\"x\":4,\"y\":4},{\"x\":20,\"y\":4},"
                      "{\"x\":20,\"y\":20},{\"x\":4,\"y\":4}]}]}]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    QImage att(64, 48, QImage::Format_RGB32);
    att.fill(Qt::darkYellow);
    dock->addAttachmentImage(att, "cat.jpg");
    win.onChatSend("outline both");
    QTRY_VERIFY(!dock->isBusy());
    QCOMPARE(mock.allBodies.size(), 1);   // nothing went out behind the turn
    QVERIFY2(!chatTranscriptHas(dock, "correction"),
             "no self-check note may reach the transcript");
    beat();
  }

  void chatMultiImageOutOfRangeAndEmptySaveWarn() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    const auto wrap = [](const char* plan) {
      return QJsonDocument(QJsonObject{{"message", QJsonObject{{"content", plan}}}})
          .toJson(QJsonDocument::Compact);
    };
    mock.queue.append(wrap(
        "{\"version\":1,\"reply\":\"The fourth one.\",\"actions\":["
        "{\"op\":\"image\",\"index\":4},{\"op\":\"filter\",\"mode\":\"bw\"}]}"));
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    QImage att(64, 48, QImage::Format_RGB32);
    att.fill(Qt::darkGreen);
    dock->addAttachmentImage(att, "cat.jpg");
    win.onChatSend("do the fourth one");
    QTRY_VERIFY(!dock->isBusy());
    QVERIFY2(chatTranscriptHas(dock, "attached image 4"),
             "an unsatisfiable index must warn, naming it");
    QCOMPARE(win.settings_.imageFilter, QStringLiteral("bw"));   // the rest still ran

    // Nothing on the canvas: the save is skipped with a warning, no project.
    win.resetToBlankEditor();   // the trash button's body, minus its confirmation
    QTRY_VERIFY(!win.canvas_->hasImage());
    const int before = int(win.projectList_.size());
    mock.queue.append(wrap(
        "{\"version\":1,\"reply\":\"Saving.\",\"actions\":[{\"op\":\"save\",\"name\":\"x\"}]}"));
    win.onChatSend("save it");
    QTRY_VERIFY(!dock->isBusy());
    QVERIFY2(chatTranscriptHas(dock, "no working image"), "an empty save must warn");
    QCOMPARE(int(win.projectList_.size()), before);
    beat();
  }

  // §10 project management from chat: a removeProject plan runs the projects
  // dialog's Delete flow — confirm included (auto-accepted here) — removing
  // exactly the named project; a DECLINED clearProjects confirm lands as a
  // "clear canceled" note with every project still in place.
  void chatRemoveProjectConfirmsAndClearDeclineNotes() {
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    const auto wrap = [](const char* plan) {
      return QJsonDocument(QJsonObject{{"message", QJsonObject{{"content", plan}}}})
          .toJson(QJsonDocument::Compact);
    };
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    // Seed two local projects to pick between.
    QImage img(24, 24, QImage::Format_RGB32);
    img.fill(Qt::darkBlue);
    const QString goneId = win.addImageProjectEntry(img, "chat del target");
    const QString keptId = win.addImageProjectEntry(img, "chat del keeper");
    QVERIFY(!goneId.isEmpty() && !keptId.isEmpty());
    const int before = int(win.projectList_.size());

    // Remove one by name; the blocking QMessageBox confirm is answered "Yes".
    mock.queue.append(wrap(
        "{\"version\":1,\"reply\":\"Removed.\",\"actions\":["
        "{\"op\":\"removeProject\",\"name\":\"chat del target\"}]}"));
    dismissModal("OK");
    win.onChatSend("delete the chat del target project");
    QTRY_VERIFY(!dock->isBusy());
    QTRY_COMPARE(int(win.projectList_.size()), before - 1);
    QVERIFY2(!win.findProject(goneId.toStdString()), "the named project must be gone");
    QVERIFY2(win.findProject(keptId.toStdString()), "the other project must remain");

    // clearProjects, confirm DECLINED: nothing removed, the note says so.
    mock.queue.append(wrap(
        "{\"version\":1,\"reply\":\"Clearing.\",\"actions\":[{\"op\":\"clearProjects\"}]}"));
    dismissModal("Cancel");
    win.onChatSend("clear all my projects");
    QTRY_VERIFY(!dock->isBusy());
    QCOMPARE(int(win.projectList_.size()), before - 1);
    QVERIFY2(chatTranscriptHas(dock, "clear canceled"),
             "a declined clear must land as a note");
    beat();
  }

  // §10 removeProject{current:true} with NOTHING saved but an image open (the
  // unsaved / incognito editor the user was looking at): answering "no saved
  // project is open" is a refusal on a technicality, so it falls back to the
  // `clear` flow behind the SAME confirm. Declined keeps the picture; accepted
  // takes the image and its lines. With a saved project open, the old path runs.
  void chatRemoveCurrentFallsBackToClearWhenNothingIsSaved() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    const auto wrap = [](const char* plan) {
      return QJsonDocument(QJsonObject{{"message", QJsonObject{{"content", plan}}}})
          .toJson(QJsonDocument::Compact);
    };
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    const char* removeCurrent =
        "{\"version\":1,\"reply\":\"Removing.\",\"actions\":["
        "{\"op\":\"removeProject\",\"current\":true}]}";

    // The reported case: an incognito editor holding an edited image — nothing
    // saved (incognito blocks the project promotion a normal load would do).
    CanvasWidget* canvas = win.canvas_;
    QVERIFY(canvas);
    win.actIncognito_->setChecked(true);
    QImage shot(120, 90, QImage::Format_RGB32);
    shot.fill(Qt::darkCyan);
    canvas->loadFromImage(shot);
    QTRY_VERIFY(canvas->hasImage());
    QVERIFY(win.incognito_ && win.activeProjectId_.isEmpty());
    stencil::core::Line line;
    line.points = {{10, 10}, {80, 40}};
    canvas->setLines({line});
    QCOMPARE(static_cast<int>(canvas->lines().size()), 1);

    // Declined: the confirm ran, nothing went.
    mock.queue.append(wrap(removeCurrent));
    dismissModal("Cancel");
    win.onChatSend("remove this project");
    QTRY_VERIFY(!dock->isBusy());
    QVERIFY2(canvas->hasImage(), "a declined confirm must keep the image");
    QCOMPARE(static_cast<int>(canvas->lines().size()), 1);
    QVERIFY2(chatTranscriptHas(dock, "removal canceled"),
             "a declined confirm is a note, never a failed plan");
    QVERIFY2(!chatTranscriptHas(dock, "no saved project is open"),
             "the technicality refusal must be gone");

    // Accepted: the §10 clear flow — image and lines go, the editor is empty.
    mock.queue.append(wrap(removeCurrent));
    dismissModal("OK");
    win.onChatSend("remove this project");
    QTRY_VERIFY(!dock->isBusy());
    QTRY_VERIFY2(!canvas->hasImage(), "the accepted fallback must clear the image");
    QCOMPARE(static_cast<int>(canvas->lines().size()), 0);

    // With a SAVED project open the old path is unchanged: the project itself goes.
    win.actIncognito_->setChecked(false);
    QImage img(24, 24, QImage::Format_RGB32);
    img.fill(Qt::darkBlue);
    const QString id = win.addImageProjectEntry(img, "chat current target");
    QVERIFY(!id.isEmpty());
    win.activeProjectId_ = id;
    const int before = int(win.projectList_.size());
    mock.queue.append(wrap(removeCurrent));
    dismissModal("OK");
    win.onChatSend("remove this project");
    QTRY_VERIFY(!dock->isBusy());
    QTRY_COMPARE(int(win.projectList_.size()), before - 1);
    QVERIFY2(!win.findProject(id.toStdString()), "the saved project must be the one removed");
    beat();
  }

  // §10 clearChat from chat: DEFERRED (the plan's other action runs first even
  // when clearChat is listed first) and always confirmed. Declined: a "clear
  // canceled" note, everything kept. Accepted: dock transcript, chatHistory_,
  // the §12 persisted copy AND the §7 text-only latch all clear.
  void chatClearChatDefersConfirmsAndClears() {
    using stencil::gui::Project;
    MainWindow win(nullptr, false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    win.settings_.saveChatsWithProject = true;
    MockChatTransport mock;
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    // A local project to file the persisted copy under (§12).
    win.adoptCanvasAsLocalProject();
    QVERIFY(!win.activeProjectId_.isEmpty());
    const QString projectId = win.activeProjectId_;
    const auto wrap = [](const char* plan) {
      return QJsonDocument(QJsonObject{{"message", QJsonObject{{"content", plan}}}})
          .toJson(QJsonDocument::Compact);
    };

    // Declined round: clearChat FIRST, units second — the units op still runs
    // (deferral), and the decline lands as a note with everything kept.
    mock.queue.append(wrap(
        "{\"version\":1,\"reply\":\"Inches it is — clearing next.\",\"actions\":["
        "{\"op\":\"clearChat\"},{\"op\":\"units\",\"value\":\"in\"}]}"));
    // The deferred confirm is QUEUED at turn end, so arm the dismissal AFTER
    // the send: its poll then runs inside the modal's own event loop.
    win.onChatSend("switch to inches, then clear the chat");
    dismissModal("Cancel");
    QTRY_VERIFY(!dock->isBusy());
    QTRY_VERIFY2(chatTranscriptHas(dock, "clear canceled"),
                 "a declined confirm must land as a note");
    QCOMPARE(win.settings_.units, QString("in"));  // ran despite being listed second
    win.applyUnits("cm");                          // tidy the persisted setting
    QCOMPARE(win.chatHistory_.size(), 2);          // user + assistant kept
    {
      Project* pr = win.findProject(projectId.toStdString());
      QVERIFY2(pr && !pr->chat.isEmpty(), "the persisted copy must survive a decline");
    }

    // Accepted round: transcript + history + persisted copy go, latch re-arms.
    win.chatTextOnlyKey_ = QStringLiteral("some|other|model");
    mock.queue.append(wrap(
        "{\"version\":1,\"reply\":\"Clearing.\",\"actions\":[{\"op\":\"clearChat\"}]}"));
    win.onChatSend("clear the chat");
    dismissModal("OK");   // after the send — the confirm is queued (see above)
    QTRY_VERIFY(!dock->isBusy());
    QTRY_VERIFY2(win.chatHistory_.isEmpty(), "the replay history must clear");
    QTRY_VERIFY2(assistantBubbleTexts(dock).isEmpty(), "the transcript must clear");
    QVERIFY2(win.chatTextOnlyKey_.isEmpty(), "the §7 text-only latch must re-arm");
    {
      Project* pr = win.findProject(projectId.toStdString());
      QVERIFY2(pr && pr->chat.isEmpty(), "the §12 persisted copy must clear (§12.2)");
    }

    // Tidy the dev state dir: drop the project this test created.
    dismissModal("OK");
    QAction* clear = actionByText(&win, "Clear Project");
    QVERIFY(clear);
    clear->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!canvas->hasImage(), 5000);
    beat();
  }

  // §7 edge map: a turn that carries the working snapshot carries a SECOND
  // image — the contour render — directly after it, with the exact suffix
  // sentence riding along, and the edge map is never replayed on later turns.
  // §3.0: a layout answer ends the turn, so ONE request goes out — the edge map
  // belongs to the main turn and to nothing else.
  void chatEdgeMapRidesAlong() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.canvas_->hasImage());
    // The fixture is a flat white image; a perfectly uniform snapshot's contour render
    // can coincide with the plain snapshot bit-for-bit (applyContourRGBA maps ANY
    // uniform image to solid white). One line breaks the uniformity so the edge map
    // and the plain snapshot are guaranteed to differ, regardless of that overlap.
    stencil::core::Line line;
    line.color = "#000000";
    line.thickness = 4;
    line.points.push_back({20.0, 20.0});
    line.points.push_back({100.0, 100.0});
    win.canvas_->setLines({line});
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"Outlined.\",\"actions\":[{\"op\":\"layout\","
                      "\"lines\":[{\"points\":[{\"x\":40,\"y\":40},{\"x\":80,\"y\":40},"
                      "{\"x\":80,\"y\":80},{\"x\":40,\"y\":40}]}]}]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    win.onChatSend("outline the box");
    QTRY_VERIFY(!win.chatDock_->isBusy());
    QCOMPARE(mock.allBodies.size(), 1);   // the turn, and nothing behind it

    const QString sentence =
        "The second attached image is an edge-map render of the working image at the "
        "same pixel coordinates: use it to place outline points on real edges.";
    const QJsonArray msgs = mock.allBodies.first().value("messages").toArray();
    const QJsonArray images = msgs.last().toObject().value("images").toArray();
    QCOMPARE(images.size(), 2);   // snapshot first, edge map second
    QVERIFY2(images.at(0).toString() != images.at(1).toString(),
             "the edge map must be a distinct (contoured) render");
    const QString sys = msgs.at(0).toObject().value("content").toString();
    QVERIFY2(sys.endsWith(sentence), "suffix must end with the exact edge-map sentence");
    QCOMPARE(sys.count(sentence), qsizetype(1));

    // The next turn replays the PRIOR turn's snapshot — never its edge map.
    mock.allBodies.clear();
    mock.response = QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content",
                                 "{\"version\":1,\"reply\":\"ok\",\"actions\":[]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.onChatSend("thanks");
    QTRY_VERIFY(!win.chatDock_->isBusy());
    const QJsonArray msgs2 = mock.allBodies.first().value("messages").toArray();
    // system, user1, assistant1, user2: the replayed user1 keeps exactly its
    // snapshot; the fresh turn carries snapshot + edge map again.
    QCOMPARE(msgs2.at(1).toObject().value("role").toString(), QString("user"));
    const QJsonArray prior = msgs2.at(1).toObject().value("images").toArray();
    QCOMPARE(prior.size(), 1);
    QCOMPARE(prior.at(0).toString(), images.at(0).toString());   // the snapshot
    QCOMPARE(msgs2.last().toObject().value("images").toArray().size(), 2);
    beat();
  }

  // §7 auto-continuation: the re-sent round carries the NEW working snapshot
  // plus its edge map (browser parity), with the exact suffix sentence — while
  // the imageless first round carried neither image nor sentence.
  void chatEdgeMapOnContinuation() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(!win.canvas_->hasImage());   // empty editor: turn 1 has no snapshot
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    // A load-only plan (blank) triggers the single §7 continuation round. A
    // COLOURED blank: the edge map contours to flat white, so a white blank's
    // snapshot would coincide with it byte-for-byte and void the ≠ check below.
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"Blank page.\",\"actions\":"
                      "[{\"op\":\"blank\",\"color\":\"#3366cc\",\"format\":\"a6\"}]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    win.onChatSend("give me a blank a6 page");
    QTRY_VERIFY(!win.chatDock_->isBusy());
    QVERIFY(win.canvas_->hasImage());
    QCOMPARE(mock.allBodies.size(), 2);   // the turn + exactly one continuation

    const QString sentence =
        "The second attached image is an edge-map render of the working image at the "
        "same pixel coordinates: use it to place outline points on real edges.";
    const QJsonArray msgs1 = mock.allBodies.first().value("messages").toArray();
    QVERIFY(!msgs1.last().toObject().contains("images"));   // nothing to snapshot yet
    QVERIFY(!msgs1.at(0).toObject().value("content").toString().contains(sentence));

    const QJsonArray msgs2 = mock.allBodies.at(1).value("messages").toArray();
    const QJsonArray images = msgs2.last().toObject().value("images").toArray();
    QCOMPARE(images.size(), 2);   // fresh snapshot + its edge map
    QVERIFY(images.at(0).toString() != images.at(1).toString());
    QVERIFY2(msgs2.at(0).toObject().value("content").toString().endsWith(sentence),
             "continuation suffix must end with the exact edge-map sentence");
    beat();
  }

  // §10 openUrl awaits the load + amended §7: a plan [openUrl, filter] must land
  // the filter on the fetched picture (no "no working image" race with the async
  // MediaLoader), then continue ONCE — it contains a load op and drew no layout.
  void chatOpenUrlAwaitsLoadThenContinues() {
    // A tiny local HTTP server serving one PNG, so MediaLoader has a real
    // download to await — fully offline.
    QImage src(20, 14, QImage::Format_RGB32);
    src.fill(QColor("#3366cc"));
    QByteArray png;
    QBuffer buf(&png);
    QVERIFY(buf.open(QIODevice::WriteOnly));
    QVERIFY(src.save(&buf, "PNG"));
    QTcpServer http;
    QVERIFY(http.listen(QHostAddress::LocalHost, 0));
    connect(&http, &QTcpServer::newConnection, this, [&http, &png] {
      QTcpSocket* s = http.nextPendingConnection();
      connect(s, &QTcpSocket::readyRead, s, [s, &png] {
        s->readAll();
        s->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: " +
                 QByteArray::number(png.size()) + "\r\nConnection: close\r\n\r\n" + png);
        s->disconnectFromHost();
      });
      connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
    });
    const QString url = QStringLiteral("http://127.0.0.1:%1/cat.png").arg(http.serverPort());

    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(!win.canvas_->hasImage());
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    // The unknown op yields a round-1 WARNING, which must survive into the
    // single final bubble (§7 one-reply parity below).
    const QString plan = QStringLiteral(
        "{\"version\":1,\"reply\":\"Loading and filtering.\",\"actions\":["
        "{\"op\":\"sparkle\"},"
        "{\"op\":\"openUrl\",\"url\":\"%1\"},{\"op\":\"filter\",\"mode\":\"bw\"}]}").arg(url);
    mock.queue.append(QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content", plan}}}}).toJson(QJsonDocument::Compact));
    // The continuation round answers chat-only.
    mock.queue.append(QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content", "All done."}}}}).toJson(QJsonDocument::Compact));
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);

    win.onChatSend(QStringLiteral("open %1, then make it b&w").arg(url));
    QTRY_VERIFY(!win.chatDock_->isBusy());
    // openUrl waited for the download: the fetched picture IS the working image…
    QVERIFY(win.canvas_->hasImage());
    QCOMPARE(win.canvas_->effectiveOriginalImage().size(), QSize(20, 14));
    // …and the filter landed on it (a grayscale pixel, not the blue source).
    const QImage out = win.canvas_->renderToImage(false);
    const QRgb px = out.pixel(out.width() / 2, out.height() / 2);
    QVERIFY2(qRed(px) == qGreen(px) && qGreen(px) == qBlue(px),
             "the b&w filter did not land on the loaded image");
    // Amended §7: the mixed load+edit plan (no layout) continued exactly once,
    // with the fresh snapshot attached.
    QCOMPARE(mock.allBodies.size(), 2);
    const QJsonArray contMsgs = mock.allBodies.at(1).value("messages").toArray();
    QVERIFY2(!contMsgs.last().toObject().value("images").toArray().isEmpty(),
             "the continuation round must attach the fresh snapshot");
    // ONE final reply (browser parity): round 1's bubble was held, its warnings
    // folded into the continuation's bubble; the intermediate reply never rendered.
    const QStringList bubbles = assistantBubbleTexts(win.chatDock_);
    QCOMPARE(bubbles.size(), 1);
    QVERIFY2(bubbles.first().contains(QStringLiteral("All done.")),
             "the single bubble must carry the FINAL round's reply");
    QVERIFY2(bubbles.first().contains(QStringLiteral("Skipped unknown op \"sparkle\".")),
             "round 1's warnings must ride in the final bubble");
    QVERIFY2(!chatTranscriptHas(win.chatDock_, QStringLiteral("Loading and filtering.")),
             "round 1's reply must not render as its own bubble");
    // …while the model-side history still keeps round 1's own answer per round.
    QCOMPARE(win.chatHistory_.at(1).text, QStringLiteral("Loading and filtering."));
    beat();
  }

  // §7 one-reply companion: a load-shaped plan HOLDS its bubble, but when the
  // continuation cannot launch (here: a text-only model, nothing to continue
  // WITH) the held reply posts right then — one bubble, nothing lost, no round 2.
  void chatHeldReplyPostsWhenContinuationSkipped() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    const stencil::llm::LlmSettings cfg = win.currentLlmSettings();
    win.chatTextOnlyKey_ =
        QStringList{cfg.provider, cfg.baseUrl, cfg.model, cfg.serverUrl}.join(QLatin1Char('|'));
    MockChatTransport mock;
    mock.response = QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"Here is your page.\",\"actions\":"
                      "[{\"op\":\"blank\",\"color\":\"#ffffff\",\"format\":\"a6\"}]}"}}}})
                        .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    win.onChatSend("blank a6 page");
    QTRY_VERIFY(!win.chatDock_->isBusy());
    QVERIFY(win.canvas_->hasImage());
    QCOMPARE(mock.allBodies.size(), 1);   // no continuation round launched
    const QStringList bubbles = assistantBubbleTexts(win.chatDock_);
    QCOMPARE(bubbles.size(), 1);
    QVERIFY2(bubbles.first().contains(QStringLiteral("Here is your page.")),
             "the held round-1 reply must flush when no continuation fires");
    beat();
  }

  // §7 regression: a ZERO-line layout op validates but draws nothing, so a
  // [blank, layout{lines:[]}] plan must still continue — counting it as "drew"
  // suppressed the very round meant to draw ("blank page — drawing now" ended
  // as an empty page and a promise).
  void chatEmptyLayoutOpStillContinues() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(!win.canvas_->hasImage());
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    mock.queue.append(QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"Blank made — drawing now.\",\"actions\":"
                      "[{\"op\":\"blank\",\"color\":\"#3366cc\",\"format\":\"a6\"},"
                      "{\"op\":\"layout\",\"lines\":[]}]}"}}}})
                          .toJson(QJsonDocument::Compact));
    mock.queue.append(QJsonDocument(QJsonObject{
        {"message", QJsonObject{{"content", "All done drawing."}}}})
                          .toJson(QJsonDocument::Compact));
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    win.onChatSend("blank 20x20 page with a smiley");
    QTRY_VERIFY(!win.chatDock_->isBusy());
    QVERIFY(win.canvas_->hasImage());
    QCOMPARE(mock.allBodies.size(), 2);   // the turn + exactly one continuation
    const QJsonArray msgs2 = mock.allBodies.at(1).value("messages").toArray();
    QVERIFY2(msgs2.last().toObject().value("content").toString().contains(
                 QStringLiteral("continue with it")),
             "round 2 must be the §7 continuation, carrying its note");
    // One final bubble (browser parity): round 1's reply was held and folded in.
    const QStringList bubbles = assistantBubbleTexts(win.chatDock_);
    QCOMPARE(bubbles.size(), 1);
    QVERIFY(bubbles.first().contains(QStringLiteral("All done drawing.")));
    beat();
  }

  // §7 companion: a layout op with REAL lines committed to its coordinates —
  // the same [blank, layout] shape must NOT continue, and (§3.0) must not send
  // anything else either: one round, done.
  void chatDrawnLayoutSuppressesContinuation() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(!win.canvas_->hasImage());
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    MockChatTransport mock;
    // Full-frame line (huge coords clamp to the blank's bounds): drawn for real.
    mock.queue.append(QJsonDocument(QJsonObject{
        {"message",
         QJsonObject{{"content",
                      "{\"version\":1,\"reply\":\"Outlined.\",\"actions\":"
                      "[{\"op\":\"blank\",\"color\":\"#3366cc\",\"format\":\"a6\"},"
                      "{\"op\":\"layout\",\"lines\":[{\"points\":[{\"x\":0,\"y\":0},"
                      "{\"x\":99999,\"y\":0},{\"x\":99999,\"y\":99999},{\"x\":0,\"y\":0}]}]}]}"}}}})
                          .toJson(QJsonDocument::Compact));
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    win.onChatSend("blank page with a box drawn on it");
    QTRY_VERIFY(!win.chatDock_->isBusy());
    QVERIFY(win.canvas_->hasImage());
    QCOMPARE(int(win.canvas_->allLines().size()), 1);   // the line really drew
    QCOMPARE(mock.allBodies.size(), 1);   // the turn, and nothing behind it
    for (const QJsonObject& b : mock.allBodies)
      for (const QJsonValue& m : b.value("messages").toArray())
        QVERIFY2(!m.toObject().value("content").toString().contains(
                     QStringLiteral("continue with it")),
                 "a plan that placed real lines must never continue");
    beat();
  }

  // Transcript follow (chat stickiness): a send scrolls fully down to the
  // pending "…"; a reply follows only while the view already sits at the
  // bottom — never yanking a user who scrolled up to read history.
  void chatTranscriptFollowsSendsAndPinnedReplies() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.chatDock_->show();
    settleLayout(win.chatDock_, 30);
    auto* scroll = win.chatDock_->findChild<QScrollArea*>();
    QVERIFY(scroll);
    auto* bar = scroll->verticalScrollBar();
    const QString filler = QStringLiteral(
        "Filler turn %1 — long enough to wrap across the bubble and give the "
        "transcript real scrollable height for the follow assertions below.");
    for (int i = 0; i < 8; ++i) {
      win.chatDock_->appendUser(filler.arg(i));
      win.chatDock_->appendAssistant(filler.arg(i + 100));
    }
    QTRY_VERIFY(bar->maximum() > 0);
    // A send lands the view at the very bottom, where the "…" card sits.
    bar->setValue(0);
    win.chatDock_->appendUser(QStringLiteral("newest question"));
    win.chatDock_->showPending();
    QTRY_VERIFY2(bar->maximum() > 0 && bar->value() == bar->maximum(),
                 "sending must scroll to the pending indicator at the bottom");
    QTest::qWait(50);   // drain the deferred scroll timers before scrolling away
    // Reading history releases the pin: a landing reply must not yank the view.
    bar->setValue(0);
    win.chatDock_->clearPending();
    win.chatDock_->appendAssistant(QStringLiteral("a reply landing mid-history"));
    QTest::qWait(80);
    QVERIFY2(bar->value() < bar->maximum() / 2,
             "a reply must not yank a reader back down from history");
    // Back at the bottom the pin re-arms: the next reply is followed.
    bar->setValue(bar->maximum());
    win.chatDock_->appendAssistant(QStringLiteral("and one the reader follows"));
    QTRY_VERIFY(bar->maximum() > 0 && bar->value() == bar->maximum());
    beat();
  }

  // §3.0: a turn ends when its plan has executed and its reply is shown — one model
  // round, nothing after it. This used to fan out one re-trace request PER LINE plus
  // up to three whole-layout self-check rounds, so a 17-line trace spent minutes
  // working (and failing) under a reply that was already on screen.
  void chatLayoutTurnIssuesExactlyOneRound() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.canvas_->hasImage());
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";

    // 17 small boxes — every one of them would have earned its own refinement
    // request, and the spread would have earned suspect rounds on top.
    QStringList lines;
    for (int i = 0; i < 17; ++i) {
      const int x = 5 + (i % 6) * 30, y = 5 + (i / 6) * 30;
      lines << QStringLiteral("{\"points\":[{\"x\":%1,\"y\":%2},{\"x\":%3,\"y\":%2},"
                              "{\"x\":%3,\"y\":%4},{\"x\":%1,\"y\":%2}]}")
                   .arg(x).arg(y).arg(x + 20).arg(y + 20);
    }
    MockChatTransport mock;
    mock.response =
        QJsonDocument(
            QJsonObject{{"message",
                         QJsonObject{{"content",
                                      QStringLiteral("{\"version\":1,\"reply\":\"Outlined.\","
                                                     "\"actions\":[{\"op\":\"layout\",\"lines\":[%1]}]}")
                                          .arg(lines.join(QLatin1Char(',')))}}}})
            .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);

    QElapsedTimer clock;
    clock.start();
    win.onChatSend("outline every box");
    QTRY_VERIFY(!win.chatDock_->isBusy());
    const qint64 settledMs = clock.elapsed();

    QCOMPARE(mock.allBodies.size(), 1);   // ONE round for the whole turn
    QCOMPARE(int(win.canvas_->allLines().size()), 17);   // …and it drew all of them
    // Nothing may be queued behind the reply either: the turn is over.
    QTest::qWait(200);
    QCOMPARE(mock.allBodies.size(), 1);
    QVERIFY2(settledMs < 2000, "the turn must settle with its reply, not minutes later");

    // No self-check / sharpening vocabulary may reach the user, in the transcript
    // or in the toast.
    const QString shown = dockText(win.chatDock_) +
                          (win.chatToast_ ? dockText(win.chatToast_) : QString());
    for (const char* word : {"sharpen", "self-check", "re-checked", "correction", "refine"})
      QVERIFY2(!shown.contains(QLatin1String(word), Qt::CaseInsensitive),
               qPrintable(QString("the reply still mentions \"%1\": %2")
                              .arg(QLatin1String(word), shown.simplified())));
    beat();
  }

  // The same rule seen from the wire: with the transport HOLDING the answer, exactly
  // one request is ever made — answering it settles the turn and parks nothing new.
  void chatLayoutTurnParksNothingAfterTheAnswer() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.canvas_->hasImage());
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";

    DeferredChatTransport deferred;
    deferred.response =
        QJsonDocument(QJsonObject{
            {"message",
             QJsonObject{{"content",
                          "{\"version\":1,\"reply\":\"Outlined.\",\"actions\":[{\"op\":\"layout\","
                          "\"lines\":[{\"points\":[{\"x\":20,\"y\":20},{\"x\":50,\"y\":20},"
                          "{\"x\":50,\"y\":50},{\"x\":20,\"y\":20}]}]}]}"}}}})
            .toJson(QJsonDocument::Compact);
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&deferred);
    win.onChatSend("outline the box");
    QCOMPARE(deferred.parked.size(), 1);   // the turn's own request, waiting
    QVERIFY(win.chatDock_->isBusy());

    deferred.answerNext();                 // …the answer lands
    QTRY_VERIFY(!win.chatDock_->isBusy());
    QCOMPARE(int(win.canvas_->allLines().size()), 1);
    QTest::qWait(200);
    QVERIFY2(deferred.parked.isEmpty(), "a follow-up round was sent behind the reply");
    QCOMPARE(deferred.started, 1);
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatTurns.gui.moc"
