// MainWindow GUI e2e — The assistant dock and its mirror panel: showing, placing, dragging,
// swapping sides, the compact popover, toasts and the menu-panel twin.
// Shared ground (helpers, the loaded window, the motion pins) is in mainWindow.gui.hpp.
#include "mainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }






  // The chat dock's resize edge (browser .chat-resizer). The strip is QMainWindow chrome
  // with no widget of its own, so a mouse-transparent band is painted over it, in whichever
  // area the dock sits and only where Qt would actually start the resize.
  void chatResizeEdgeFollowsTheDockInEveryArea() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(win.chatDock_);
    win.chatDock_->show();
    settleLayout(&win, 120);
    auto* edge = win.chatEdge_;
    QVERIFY(edge);
    const struct { Qt::DockWidgetArea area; Qt::Orientation split; const char* name; } kAreas[] = {
        {Qt::LeftDockWidgetArea, Qt::Horizontal, "left"},
        {Qt::RightDockWidgetArea, Qt::Horizontal, "right"},
        {Qt::TopDockWidgetArea, Qt::Vertical, "top"},
        {Qt::BottomDockWidgetArea, Qt::Vertical, "bottom"},
    };
    for (const auto& a : kAreas) {
      win.addDockWidget(a.area, win.chatDock_, a.split);
      settleLayout(&win, 120);
      const QRect dock = win.chatDock_->geometry();
      const QRect hit = win.chatEdgeHit_;
      const QRect band = edge->geometry();
      QVERIFY2(edge->isVisible(), a.name);
      QVERIFY2(!hit.isEmpty(), a.name);
      // The strip sits OUTSIDE the panel, against the edge it is docked by.
      QVERIFY2(!hit.intersects(dock), a.name);
      if (a.area == Qt::LeftDockWidgetArea) QCOMPARE(hit.left(), dock.right() + 1);
      if (a.area == Qt::RightDockWidgetArea) QCOMPARE(hit.right(), dock.left() - 1);
      if (a.area == Qt::TopDockWidgetArea) QCOMPARE(hit.top(), dock.bottom() + 1);
      if (a.area == Qt::BottomDockWidgetArea) QCOMPARE(hit.bottom(), dock.top() - 1);
      // …and the band is drawn AROUND that strip, never thinner than an affordance can
      // be seen at (the horizontal separators are a hairline by design, theme.cpp).
      QVERIFY2(band.contains(hit), a.name);
      QVERIFY2(qMin(band.width(), band.height()) >= stencil::gui::DockEdgeOverlay::kMinThickness, a.name);
    }
    // Nothing to grab while it floats — the window frame owns that resize.
    win.chatDock_->setFloating(true);
    QTRY_VERIFY(!edge->isVisible());
  }

  // The assistant's settings (the chat's … ▸ Settings) have a chord of their own, from the
  // shared registry: it opens the assistant-only dialog with no chat surface up at all, and
  // pressed again inside that dialog it closes it — the toolbar windows' toggle rule.
  void assistantSettingsShortcutOpensAndClosesTheDialog() {
    MainWindow win(nullptr, false);
    openLoaded(win);
    QVERIFY2(!win.actAssistantSettings_->shortcut().isEmpty(), "the dialog has a chord");
    QCOMPARE(win.hotkeyLabels_.value(QStringLiteral("openAssistantSettings")),
             QStringLiteral("AI Assistant Settings"));   // the Shortcuts window lists it
    QVERIFY(!win.chatDock_->isVisible());

    QString dialogName;
    bool sawOwn = false, closedByOwn = false;
    QTimer::singleShot(0, &win, [&] {
      QDialog* dlg = nullptr;
      for (int i = 0; i < 200 && !dlg; ++i) {
        dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dlg) QTest::qWait(10);
      }
      if (!dlg) return;
      dialogName = dlg->objectName();
      for (QShortcut* sc : dlg->findChildren<QShortcut*>()) {
        if (sc->key() == win.actAssistantSettings_->shortcut()) { sawOwn = true; emit sc->activated(); }
      }
      closedByOwn = !dlg->isVisible();
      if (!closedByOwn) dlg->reject();
    });
    win.actAssistantSettings_->trigger();   // blocks in exec() until the timer closes it

    QCOMPARE(dialogName, QString("assistantSettingsDialog"));
    QVERIFY2(sawOwn, "the dialog carried its own opener's chord");
    QVERIFY2(closedByOwn, "its own shortcut closed the window");
    beat();
  }

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

  // The AI-Assistant chat dock: the checkable toolbar/View action opens and closes it;
  // unlike the deliberately pinned selection panel it is dockable on all four sides and
  // floatable/closable, defaults LEFT on a fresh run, hosts the transcript/input in a
  // user-resizable splitter, gates Send on input/busy state, and turns clipboard-pasted
  // images into attachments (plain text pastes normally).
  void chatDockToggles() {
    // A saved dock layout would restore whatever area the last run used; clear it so
    // this asserts the FIRST-RUN default (left, matching the browser).
    {
      stencil::gui::Settings s = stencil::gui::fileStore::loadSettings();
      s.windowState.clear();
      stencil::gui::fileStore::saveSettings(s);
    }
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    QVERIFY(dock);
    QCOMPARE(dock->allowedAreas(), Qt::AllDockWidgetAreas);
    QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetMovable));
    QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetFloatable));
    QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetClosable));
    QCOMPARE(win.dockWidgetArea(dock), Qt::LeftDockWidgetArea);  // first-run default
    // The COMPOSER acts on a drop; the DOCK swallows the ones that miss it, so a
    // gesture aimed at the chat can never reach the window and offer to open the
    // image as a project (browser chatPanel.js parity). The composer's QPlainTextEdit
    // declines drops too, or it would swallow one and paste the path as text.
    QVERIFY(dock->acceptDrops());
    auto* inputArea = dock->findChild<QWidget*>("chatInputArea");
    QVERIFY(inputArea);
    QVERIFY(inputArea->acceptDrops());  // image/video drops become chat attachments
    auto* chatInput = dock->findChild<QPlainTextEdit*>("chatInput");
    QVERIFY(chatInput && !chatInput->acceptDrops() && !chatInput->viewport()->acceptDrops());
    // …cued by an animated icon over the composer, hidden until a drag arrives.
    auto* cue = dock->findChild<QWidget*>("chatDropCue");
    QVERIFY(cue && cue->isHidden());

    // The composer's resize grip is the SHARED pill (pillSplitter.hpp), not a
    // stylesheet handle: the stylesheet one could only be narrowed by a symmetric
    // margin, so it stretched with the panel into a fat accent band across the dock.
    auto* chatSplitter = dock->findChild<QSplitter*>("chatSplitter");
    QVERIFY(chatSplitter);
    QVERIFY2(dynamic_cast<stencil::gui::PillSplitterHandle*>(chatSplitter->handle(1)) != nullptr,
             "the dock's grip is not the shared pill handle");
    // A grab of the empty dock, for eyeballing the composer end-to-end.
    if (qEnvironmentVariableIsSet("STENCIL_GUI_SHOTS")) {
      dock->resize(360, 520);
      QTest::qWait(50);
      dock->grab().save(QString::fromLocal8Bit(qgetenv("STENCIL_GUI_SHOTS")) + "/dock-empty.png");
    }

    // Resizable input area: transcript over input in a vertical, non-collapsible splitter.
    auto* splitter = dock->findChild<QSplitter*>("chatSplitter");
    QVERIFY(splitter);
    QCOMPARE(splitter->orientation(), Qt::Vertical);
    QCOMPARE(splitter->count(), 2);
    QVERIFY(!splitter->childrenCollapsible());

    // By objectName, NOT by text: the dock's internal toggleViewAction shares
    // the "AI Assistant" label.
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(chat);
    QVERIFY(chat->isCheckable());
    chat->setChecked(false);         // normalize (a restored layout may have opened it)
    QTRY_VERIFY(!dock->isVisible());
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());

    // The sparkle toolbar button mirrors actChat (setDefaultAction): same
    // toggle, checked while the dock is open (browser sparkle-button parity).
    QToolButton* chatBtn = nullptr;
    for (QToolButton* b : win.findChildren<QToolButton*>())
      if (b->defaultAction() == chat) { chatBtn = b; break; }
    QVERIFY(chatBtn);
    QVERIFY(chatBtn->isChecked());
    chatBtn->click();
    QTRY_VERIFY(!dock->isVisible());
    chatBtn->click();
    QTRY_VERIFY(dock->isVisible());

    // Right-side docking must work even though the fixed-width selection panel owns
    // that area — nesting provides the drop slots (regression: the chat dock could
    // not be pinned to the right at all).
    QVERIFY(win.isDockNestingEnabled());
    auto* selPanel = win.findChild<QDockWidget*>("selectionPanelDock");
    QVERIFY(selPanel && win.dockWidgetArea(selPanel) == Qt::RightDockWidgetArea);
    win.addDockWidget(Qt::RightDockWidgetArea, dock);
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::RightDockWidgetArea);
    QTRY_VERIFY(dock->isVisible());
    win.addDockWidget(Qt::LeftDockWidgetArea, dock);  // restore for the rest of the slot
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::LeftDockWidgetArea);

    // First tear-off adopts the compact default size (not the docked span that
    // used to stretch the floating panel across the whole window).
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    QTRY_COMPARE(dock->size(), QSize(385, 480));   // +5px of room for the row "…"
    dock->setFloating(false);
    QTRY_VERIFY(!dock->isFloating());
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::LeftDockWidgetArea);

    // Send gating: disabled while the input is empty, enabled once text lands.
    auto* input = dock->findChild<QPlainTextEdit*>("chatInput");
    auto* send = dock->findChild<QToolButton*>("chatSend");
    QVERIFY(input && send);
    auto* gearBtn = dock->findChild<QToolButton*>("chatGear");  // settings target
    auto* moreBtn = dock->findChild<QToolButton*>("chatMore");   // the … overflow
    QVERIFY(gearBtn && moreBtn);
    // The status dot is a BADGE on the … TRIGGER (browser .conn-status parity):
    // child of the button, no layout slot of its own. The gear itself now lives
    // in the menu, so it is hidden — the dot has to ride what stays visible.
    auto* dot = dock->findChild<QLabel*>("chatStatusDot");
    QVERIFY(dot && dot->parentWidget() == moreBtn);
    // Composer = send + …, in that order, both the filled-accent buttons.
    QVERIFY(send->x() < moreBtn->x());
    QVERIFY(send->property("chatAccent").toBool());
    QVERIFY(moreBtn->property("chatAccent").toBool());
    // Attach / clear / settings are reachable ONLY through the … menu now.
    auto* attachBtn = dock->findChild<QToolButton*>("chatAttach");
    auto* clearInRow = dock->findChild<QToolButton*>("chatClear");
    QVERIFY(attachBtn && clearInRow);
    QVERIFY(attachBtn->isHidden() && gearBtn->isHidden());
    QVERIFY(moreBtn->menu());
    QStringList items;
    for (QAction* a : moreBtn->menu()->actions())
      if (!a->isSeparator()) items << a->text();
    QCOMPARE(items, (QStringList{"Add image", "Clear history", "Swap message sides", "Settings"}));
    // The branded header IS the title bar (no double header).
    QVERIFY(dock->titleBarWidget());
    QVERIFY(dock->titleBarWidget()->findChild<QLabel*>("chatHeaderTitle"));

    // Card container: the dock content renders on the controls-panel colour —
    // DISTINCT from the canvas backdrop behind it — with the themed 1px
    // border styling applied to the whole card.
    {
      const QImage bodyImg = dock->widget()->grab().toImage();
      const QColor cardBg = bodyImg.pixelColor(bodyImg.width() / 2, 4);
      const QImage centralImg = win.centralWidget()->grab().toImage();
      // Near the BOTTOM, not the exact vertical center: the top bars (toolbars,
      // image-info strip) are a few rows tall and grow/shrink with theme/content
      // changes — a center sample can drift onto one of them by coincidence. The
      // bottom stays safely inside the canvas/page area regardless.
      const QColor canvasBg =
          centralImg.pixelColor(centralImg.width() / 2, centralImg.height() - 10);
      QVERIFY(cardBg != canvasBg);
      QVERIFY(dock->styleSheet().contains("border:1px solid"));
    }
    QVERIFY(!send->isEnabled());
    input->setPlainText("make it sepia");
    QVERIFY(send->isEnabled());
    input->clear();
    QVERIFY(!send->isEnabled());

    // Clipboard paste: an image on the clipboard becomes an attachment…
    auto* chatDock = qobject_cast<stencil::gui::ChatDock*>(dock);
    QVERIFY(chatDock);
    QCOMPARE(chatDock->attachedImages().size(), 0);
    QImage clip(20, 10, QImage::Format_RGB32);
    clip.fill(Qt::red);
    QGuiApplication::clipboard()->setImage(clip);
    input->setFocus();
    QTest::keySequence(input, QKeySequence::Paste);
    QCOMPARE(chatDock->attachedImages().size(), 1);
    QVERIFY(input->toPlainText().isEmpty());  // consumed as an attachment, not text
    // …while plain text still pastes normally (no extra attachment).
    QGuiApplication::clipboard()->setText("hello there");
    QTest::keySequence(input, QKeySequence::Paste);
    QCOMPARE(input->toPlainText(), QString("hello there"));
    QCOMPARE(chatDock->attachedImages().size(), 1);
    chatDock->clearAttachments();
    input->clear();

    // Cohesive-column chrome (browser parity): in-content header title and a
    // BORDERLESS transcript (browser .chat-transcript parity — the card look
    // comes from the dock's own panel background + border, not an inner frame).
    QVERIFY(dock->findChild<QLabel*>("chatHeaderTitle"));
    auto* scrollArea = dock->findChild<QScrollArea*>();
    QVERIFY(scrollArea && scrollArea->frameShape() == QFrame::NoFrame);

    // Empty-state suggestion chips: shown while the transcript is empty; a
    // click PREFILLS the composer (never sends); gone once the first card lands.
    auto* suggest = dock->findChild<QWidget*>("chatSuggest");
    QVERIFY(suggest && suggest->isVisible());
    // Chips lay out INLINE with wrapping (flow layout), not one per row.
    QVERIFY(suggest->layout());
    QVERIFY(!qobject_cast<QVBoxLayout*>(suggest->layout()));
    QVERIFY(suggest->layout()->hasHeightForWidth());
    const auto chips = dock->findChildren<QPushButton*>("chatSuggestChip");
    QCOMPARE(chips.size(), 4);
    chips.first()->click();
    QCOMPARE(input->toPlainText(), QString("Make it sepia"));
    QVERIFY(send->isEnabled());  // prefilled, not sent
    QVERIFY(suggest->isVisible());
    input->clear();
    chatDock->appendAssistant("done");  // first transcript card
    QVERIFY(!suggest->isVisible());

    // The attach-routing sniffers the paste/drop flows share (mediaLoader).
    QVERIFY(stencil::gui::isVideoFileName("clip.MP4"));
    QVERIFY(stencil::gui::isVideoFileName("/tmp/a.webm"));
    QVERIFY(!stencil::gui::isVideoFileName("photo.png"));
    QVERIFY(stencil::gui::isImageFileName("photo.JPEG"));
    QVERIFY(!stencil::gui::isImageFileName("clip.mp4"));

    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
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
    // (kEnteringProperty), so that IS the slide's own completion flag.
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

  // The chat toolbar icon answers the same popover gestures as every dialog icon
  // (browser chatPanel.js openCompact): a double-click opens a COMPACT FLOATING chat
  // pinned by the icon, and re-pins it when already open. A plain click still toggles,
  // deferred one double-click interval (whose trailing-release swallow this exercises).
  void chatIconPopoverGesture() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    win.raise();
    win.activateWindow();   // the typing-guard check below needs a focus owner
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(dock && chat);
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    QToolButton* btn = nullptr;
    for (QToolButton* b : win.findChildren<QToolButton*>())
      if (b->defaultAction() == chat) { btn = b; break; }
    QVERIFY(btn);

    // A real double-click is press, release, dblclick, release — all four must go
    // through the gesture filter (QTest::mouseDClick waits internally, which the
    // deferred-click timer turns into a plain click first).
    const QPoint hit = btn->rect().center();
    const auto send = [btn, hit](QEvent::Type t) {
      QMouseEvent e(t, QPointF(hit), btn->mapToGlobal(hit), Qt::LeftButton,
                    t == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                    Qt::NoModifier);
      QApplication::sendEvent(btn, &e);
    };
    const auto doubleClick = [&send] {
      send(QEvent::MouseButtonPress);
      send(QEvent::MouseButtonRelease);
      send(QEvent::MouseButtonDblClick);
      send(QEvent::MouseButtonRelease);
    };

    // While a text control has FOCUS, Alt belongs to the typing: with the cursor
    // resting on the icon, pressing Alt must NOT open the peek. Checked FIRST —
    // before any floating-dock activation, which the offscreen platform cannot
    // hand back (no raise()), leaves setFocus without an active window. The zoom
    // combo's inner line edit is the focusable text control used for it — which
    // needs an image, since the ZOOM cluster is dead without one.
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.zoom_->isEnabled());
    QCursor::setPos(btn->mapToGlobal(hit));
    QLineEdit* zoomEdit = win.zoom_->lineEdit();
    QVERIFY(zoomEdit);
    zoomEdit->setFocus();
    // The guard reads QApplication::focusWidget() — the editable combo is its
    // line edit's FOCUS PROXY, so that is what focus lands on.
    QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget*>(win.zoom_));
    QTest::keyPress(zoomEdit, Qt::Key_Alt);
    QTest::keyRelease(zoomEdit, Qt::Key_Alt);
    QTest::qWait(50);
    QVERIFY2(!dock->isVisible(), "Alt while typing must not open the peek");
    win.zoom_->clearFocus();
    QTRY_VERIFY(QApplication::focusWidget() != win.zoom_);

    doubleClick();
    QTRY_VERIFY(dock->isVisible());
    QVERIFY2(dock->isFloating(), "the popover gesture must float the dock, not slide it in docked");
    QVERIFY(chat->isChecked());
    // Pinned next to the icon at the compact size — the shared placement rule.
    const QRect btnRect(btn->mapToGlobal(QPoint(0, 0)), btn->size());
    const QRect expect = stencil::support::popoverRect(
        btnRect, win.chatDock_->floatingDefaultSize(), btn->screen()->availableGeometry());
    QTRY_COMPARE(dock->geometry().topLeft(), expect.topLeft());
    QCOMPARE(dock->size(), expect.size());
    // The deferred single-click must NOT fire off the dblclick's trailing release
    // and yank the chat back shut (the swallow-release regression).
    QTest::qWait(QApplication::doubleClickInterval() + 300);
    QVERIFY2(dock->isVisible(), "the dblclick's trailing release must not re-arm the deferred toggle");
    QVERIFY(chat->isChecked());

    // The gesture while ALREADY open re-pins compact — it never hides.
    doubleClick();
    QTRY_VERIFY(dock->isVisible());
    QVERIFY(dock->isFloating());
    QVERIFY(chat->isChecked());

    // A plain click still toggles it off — after the double-click interval.
    send(QEvent::MouseButtonPress);
    send(QEvent::MouseButtonRelease);
    QTRY_VERIFY_WITH_TIMEOUT(!dock->isVisible(), QApplication::doubleClickInterval() + 2000);
    QVERIFY(!chat->isChecked());

    // Alt while hovering (the peek route, browser altHover parity): rest the
    // cursor on the icon and press Alt — the compact float opens with no click —
    // and it is HOLD-to-peek: releasing Alt closes it again.
    QCursor::setPos(btn->mapToGlobal(hit));
    QTest::qWait(20);
    QTest::keyPress(&win, Qt::Key_Alt);
    QTRY_VERIFY(dock->isVisible());
    QVERIFY(dock->isFloating());
    QVERIFY(chat->isChecked());
    QTest::keyRelease(&win, Qt::Key_Alt);
    QTRY_VERIFY2(!dock->isVisible(), "releasing Alt must close what the peek opened");
    QVERIFY(!chat->isChecked());

    // ENGAGED peek: move the cursor INTO the peeked window before releasing Alt —
    // it stays open (sticky), instead of being yanked out from under the user.
    QCursor::setPos(btn->mapToGlobal(hit));
    QTest::qWait(20);
    QTest::keyPress(&win, Qt::Key_Alt);
    QTRY_VERIFY(dock->isVisible());
    QCursor::setPos(dock->frameGeometry().center());
    QTest::qWait(20);
    QTest::keyRelease(&win, Qt::Key_Alt);
    QTest::qWait(50);
    QVERIFY2(dock->isVisible(), "releasing Alt with the cursor inside must keep the peek open");
    QVERIFY(chat->isChecked());
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());

    // A DELIBERATE (dblclick) open is sticky: an Alt press+release leaves it be.
    doubleClick();
    QTRY_VERIFY(dock->isVisible());
    QTest::keyPress(&win, Qt::Key_Alt);
    QTest::keyRelease(&win, Qt::Key_Alt);
    QTest::qWait(50);
    QVERIFY2(dock->isVisible(), "Alt release must never close a dblclick-opened popover");
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());

    // A DISABLED icon opens nothing — mini window included — and must not arm a
    // stale popover anchor that would pin the NEXT dialog to it.
    QCursor::setPos(btn->mapToGlobal(hit));
    QTest::qWait(20);
    chat->setEnabled(false);
    doubleClick();
    QTest::qWait(80);
    QVERIFY2(!dock->isVisible(), "dblclick on a disabled icon must not open the popover");
    QTest::keyPress(&win, Qt::Key_Alt);
    QTest::keyRelease(&win, Qt::Key_Alt);
    QTest::qWait(50);
    QVERIFY2(!dock->isVisible(), "Alt-peek on a disabled icon must not open the popover");
    chat->setEnabled(true);

    dock->setFloating(false);  // leave the shared default placement for later slots
    beat();
  }

  // The chat dock is session-transient (browser full-reset-on-reload parity): a
  // saved window layout must NOT resurrect it — every launch starts hidden at
  // the default left placement. The selection panel DOES restore, and its
  // toggle action must track the restored visibility so the header chevron
  // ("Hide panel", Alt+X) still works after a restart (regression: the pre-show
  // isVisible() sync left the action unchecked, so the chevron no-opped).
  void chatDockSessionTransientAndPanelToggleAfterRestore() {
    {
      stencil::gui::Settings s = stencil::gui::fileStore::loadSettings();
      s.windowState.clear();
      stencil::gui::fileStore::saveSettings(s);
    }
    // Session 1: open + float the chat dock, then persist the layout the way
    // closeEvent does.
    {
      MainWindow win(nullptr, false);
      win.resize(1200, 800);
      win.show();
      QVERIFY(QTest::qWaitForWindowExposed(&win));
      auto* dock = win.findChild<QDockWidget*>("llmChatDock");
      auto* chat = win.findChild<QAction*>("actChat");
      QVERIFY(dock && chat);
      chat->setChecked(true);
      QTRY_VERIFY(dock->isVisible());
      dock->setFloating(true);
      QTRY_VERIFY(dock->isFloating());
      stencil::gui::Settings s = stencil::gui::fileStore::loadSettings();
      s.windowState = QString::fromLatin1(win.saveState().toBase64());
      stencil::gui::fileStore::saveSettings(s);
    }
    // Session 2: chat dock reset to hidden/docked-left/unchecked; the selection
    // panel's toggle is synced to the RESTORED visibility and the chevron works.
    {
      MainWindow win(nullptr, false);
      win.resize(1200, 800);
      win.show();
      QVERIFY(QTest::qWaitForWindowExposed(&win));
      auto* dock = win.findChild<QDockWidget*>("llmChatDock");
      QVERIFY(dock);
      QVERIFY(dock->isHidden());
      QVERIFY(!dock->isFloating());
      QCOMPARE(win.dockWidgetArea(dock), Qt::LeftDockWidgetArea);
      auto* chat = win.findChild<QAction*>("actChat");
      QVERIFY(chat && !chat->isChecked());

      auto* selPanel = win.findChild<QDockWidget*>("selectionPanelDock");
      QVERIFY(selPanel && !selPanel->isHidden());
      QAction* panelAct = nullptr;
      for (QAction* a : win.findChildren<QAction*>())
        if (a->text() == "Selection Panel") { panelAct = a; break; }
      QVERIFY(panelAct);
      QVERIFY(panelAct->isChecked());  // synced to the restored visibility
      // Header chevron path: collapseRequested → actPanel_ → animated hide.
      QVERIFY(QMetaObject::invokeMethod(selPanel, "collapseRequested"));
      QTRY_VERIFY(selPanel->isHidden());
      QVERIFY(!panelAct->isChecked());
    }
    {
      stencil::gui::Settings s = stencil::gui::fileStore::loadSettings();
      s.windowState.clear();
      stencil::gui::fileStore::saveSettings(s);
    }
    beat();
  }

  // Assistant completions landing while the chat dock is HIDDEN surface as a
  // clickable bottom-left toast (browser closedToast parity): success/failure
  // text truncated to ~90 chars, auto-anchored bottom-left, click = open the
  // dock + dismiss; an open dock shows no toast.
  void chatToastWhenDockHidden() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(dock && chat);
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());

    // Success (chat-only plan) while hidden → success toast with the reply.
    stencil::llm::LlmReply ok;
    ok.ok = true;
    ok.text = "{\"version\":1,\"reply\":\"All done, boss\",\"actions\":[]}";
    win.onChatReply(ok);
    auto* toast = win.findChild<QWidget*>("chatToast");
    QVERIFY(toast);
    QTRY_VERIFY(toast->isVisible());
    auto* label = toast->findChild<QLabel*>();
    QVERIFY(label);
    QVERIFY(label->text().contains("Assistant finished"));
    QVERIFY(label->text().contains("All done, boss"));
    // Anchored bottom-left (18 px inset).
    QCOMPARE(toast->x(), 18);
    QVERIFY(toast->geometry().bottom() > win.height() - 40);

    // Click → the dock opens through the normal path and the toast dismisses.
    QTest::mouseClick(toast, Qt::LeftButton);
    QTRY_VERIFY(dock->isVisible());
    QVERIFY(chat->isChecked());
    QTRY_VERIFY(!toast->isVisible());

    // Failure while hidden → error toast, truncated to the ~90-char bound.
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    stencil::llm::LlmReply bad;
    bad.ok = false;
    bad.failure = stencil::llm::LlmFailure::Http;
    bad.error = QString(200, QChar('x'));
    win.onChatReply(bad);
    QTRY_VERIFY(toast->isVisible());
    QVERIFY(label->text().startsWith("Assistant failed"));
    QVERIFY(label->text().size() <= 90);
    QVERIFY(label->text().endsWith(QChar(0x2026)));
    QTest::mouseClick(toast, Qt::LeftButton);  // dismiss + reopen for the next check
    QTRY_VERIFY(dock->isVisible());
    QTRY_VERIFY(!toast->isVisible());

    // Open dock: completions must NOT raise a toast.
    win.onChatReply(ok);
    QVERIFY(!toast->isVisible());
    beat();
  }

  // The chat dock SLIDES in and out (browser chat-panel parity) instead of
  // popping: docked left, its WIDTH animates 0 → natural on open and back to 0
  // on close (then it hides). Sampled like fullscreenRevealAnimatesSmoothly.
  // Afterwards the size constraints must be released again (the min==max
  // pinning is animation-only) so the dock stays user-resizable, and a reopen
  // returns to the extent it had before the dismissal. Floating docks are their
  // own windows, so they just show/hide.
  void chatDockSlideAnimation() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(dock && chat);
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    QCOMPARE(win.dockWidgetArea(dock), Qt::LeftDockWidgetArea);  // width axis

    // Sample the docked extent every ~16 ms across the slide (a hidden dock
    // counts as 0 — that IS its contribution to the layout).
    auto sample = [&] {
      QList<int> s;
      QElapsedTimer t;
      t.start();
      do {
        s.append(dock->isVisible() ? dock->width() : 0);
        QTest::qWait(16);
      } while (win.chatAnim_ && t.elapsed() < 1200);
      return s;
    };
    auto hasIntermediate = [](const QList<int>& s, int full) {
      for (int v : s) if (v > 2 && v < full - 2) return true;
      return false;
    };

    // ── open: 0 → natural ──
    chat->setChecked(true);
    QVERIFY(dock->isVisible());  // shown immediately; it grows into place
    const QList<int> up = sample();
    QTRY_VERIFY(dock->width() > 200);
    const int full = dock->width();
    QVERIFY2(hasIntermediate(up, full),
             "chat dock popped open (no intermediate widths)");
    // The pinning is released at the end: still resizable, and the dock's own
    // 260 px floor is back (not clamped to the animation's last frame).
    QTRY_COMPARE(dock->maximumWidth(), QWIDGETSIZE_MAX);
    QCOMPARE(dock->minimumWidth(), 260);

    // ── close: natural → 0, then hidden ──
    chat->setChecked(false);
    QVERIFY(dock->isVisible());  // still on screen while it slides out
    const QList<int> down = sample();
    QTRY_VERIFY(!dock->isVisible());
    QVERIFY2(hasIntermediate(down, full),
             "chat dock popped shut (no intermediate widths)");
    for (int i = 1; i < down.size(); ++i)
      QVERIFY2(down[i] <= down[i - 1] + 1, "chat dock hide width oscillated");
    QTRY_COMPARE(dock->maximumWidth(), QWIDGETSIZE_MAX);

    // Reopening restores the extent it was dismissed at.
    chat->setChecked(true);
    QTRY_VERIFY(dock->width() > 200);
    awaitAnim(win.chatAnim_);
    QVERIFY2(qAbs(dock->width() - full) <= 8, "reopen lost the remembered width");

    // Floating: no edge to slide from — plain show/hide, and the tear-off size
    // is never clamped by a leftover animation constraint.
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    chat->setChecked(false);
    QVERIFY(!dock->isVisible());  // immediate, no slide
    chat->setChecked(true);
    QVERIFY(dock->isVisible());
    QTRY_COMPARE(dock->size(), QSize(385, 480));   // +5px of room for the row "…"
    dock->setFloating(false);
    QTRY_VERIFY(!dock->isFloating());
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    beat();
  }

  // The chat dock's gear opens the DEDICATED assistant dialog (browser
  // llmSettingsModal parity) — provider / base URL / model / API key / server
  // only, none of the full Settings sheet's unrelated controls — and saving it
  // round-trips the provider through the normal persistence path.
  void chatGearOpensAssistantOnlySettings() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* chat = win.findChild<QAction*>("actChat");
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    QVERIFY(chat && dock);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    auto* gear = dock->findChild<QToolButton*>("chatGear");
    QVERIFY(gear);

    const stencil::gui::Settings before = stencil::gui::fileStore::loadSettings();
    // Pick a provider that differs from whatever is configured now.
    const QString target =
        win.settings_.llmProvider == QLatin1String("openai-compat") ? "ollama"
                                                                   : "openai-compat";
    // The dialog is modal (exec blocks the click), so inspect + drive it from a
    // timer, the way dismissModal does for the confirmations.
    bool inspected = false, wrongControls = false, sawProvider = false;
    QString dialogName;
    QTimer::singleShot(0, [&] {
      QDialog* dlg = nullptr;
      for (int i = 0; i < 200 && !dlg; ++i) {
        dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dlg) QTest::qWait(10);
      }
      if (!dlg) return;
      dialogName = dlg->objectName();
      auto* provider = dlg->findChild<QComboBox*>("llmProvider");
      sawProvider = provider != nullptr;
      // Assistant-only: none of the full Settings dialog's controls (autosave /
      // visibility checkboxes, thickness & page-size spin boxes, the page-size
      // combo) may be here. The one checkbox that DOES belong is the §12
      // save-chats opt-in (llmSaveChats) — an assistant setting.
      wrongControls = !dlg->findChildren<QDoubleSpinBox*>().isEmpty() ||
                      !dlg->findChildren<QSpinBox*>().isEmpty();
      for (QCheckBox* cb : dlg->findChildren<QCheckBox*>())
        if (cb->objectName() != QLatin1String("llmSaveChats")) wrongControls = true;
      for (QComboBox* c : dlg->findChildren<QComboBox*>())
        if (c->findData(QString("A4")) >= 0 || c->findText("A4") >= 0)
          wrongControls = true;
      // The LLM rows the browser modal has, all present.
      for (const char* name : {"llmBaseUrl", "llmModel", "llmApiKey", "llmServer"})
        if (!dlg->findChild<QWidget*>(name)) wrongControls = true;
      if (provider) {
        const int idx = provider->findData(target);
        if (idx >= 0) provider->setCurrentIndex(idx);
      }
      inspected = true;
      dlg->accept();  // Save
    });
    gear->click();  // blocks in exec() until the timer accepts

    QVERIFY2(inspected, "the gear did not open a modal dialog");
    QCOMPARE(dialogName, QString("assistantSettingsDialog"));
    QVERIFY(sawProvider);
    QVERIFY2(!wrongControls, "the assistant dialog carries unrelated settings");
    // Saved through the same path as the full dialog: live settings + the file.
    QCOMPARE(win.settings_.llmProvider, target);
    QCOMPARE(stencil::gui::fileStore::loadSettings().llmProvider, target);

    stencil::gui::fileStore::saveSettings(before);  // leave the user's config alone
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
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
                                       stencil::gui::LlmSettingsForm::RowMode::HideRows);

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
          if (a->text() == "Assistant") parent = a;
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
    bad.failure = stencil::llm::LlmFailure::Http;
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
    canceled.failure = stencil::llm::LlmFailure::Transport;
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
                              stencil::gui::DisintegrateOverlay::kMs + 3000);
    QTRY_VERIFY2_WITH_TIMEOUT(!menuChips->isHidden(),
                              "the menu's chips did not come back after Clear",
                              stencil::gui::DisintegrateOverlay::kMs + 3000);
    QCOMPARE(menuChips->findChildren<QPushButton*>("chatSuggestChip").size(), 4);

    win.llmClient_.reset();
    beat();
  }

  // The chat composer's "…" was the last popup in the app that still hard-cut on both
  // edges, and the Assistant window it raises grew out of nothing — its Settings item is
  // gone by the time the window opens, so the anchor measured 0x0. Both now belong to the
  // "…" TRIGGER, which is also what makes the window fall from above when the dock is
  // shut: a hidden anchor is no anchor (modalReveal originRect).
  void chatOverflowAndItsWindowFlyToTheDotsTrigger() {
    MainWindow win;
    win.resize(1400, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.chatDock_->setVisible(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    settleLayout(&win, 150);
    // ctest runs with STENCIL_NO_ANIM=1 and every flight is a no-op under it; this test
    // is about the flight itself, so turn it back on for the duration.
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });

    auto* moreBtn = win.chatDock_->moreButton();
    QVERIFY(moreBtn && moreBtn->isVisible());
    QMenu* menu = moreBtn->menu();
    QVERIFY(menu);
    const QPoint want = flightPointOf(moreBtn, &win);

    // The menu's own dust is skipped offscreen by design (menuReveal revealPopup — the
    // gui suite picks items the instant the popup lands), so what is checkable here is
    // that BOTH edges are wired, and wired to the trigger. It is a repeat-show menu, so
    // the one-shot MenuReveal would have been wrong; revealMenuFrom is what it gets.
    auto* flight = menu->findChild<QObject*>(QStringLiteral("stencilMenuFlight"),
                                             Qt::FindDirectChildrenOnly);
    QVERIFY2(flight, "the … menu has no flight wired at all");

    // It is four short labelled icons, NOT a menu-bar menu: the theme's gutters (24px
    // left check reserve + 26px right shortcut slack) plus the shortcut column Qt
    // reserves anyway left a visible gap after each glyph and a band of dead space down
    // the right edge. compactIconMenu hugs the longest label instead.
    menu->popup(moreBtn->mapToGlobal(moreBtn->rect().bottomLeft()));
    QVERIFY(QTest::qWaitForWindowExposed(menu));
    settleLayout(menu, 30);
    int widest = 0;
    for (QAction* a : menu->actions())
      widest = std::max(widest, menu->fontMetrics().horizontalAdvance(a->text()));
    QVERIFY2(widest > 0, "no labels to measure");
    const int slack = menu->width() - widest;
    // Icon + paddings only. The untamed hint ran ~95px past the label on this font.
    QVERIFY2(slack > 0 && slack <= 64,
             qPrintable(QString("menu is %1 wide for a %2 label — %3px of slack")
                            .arg(menu->width()).arg(widest).arg(slack)));
    menu->hide();
    QTRY_VERIFY(!menu->isVisible());
    // Popping it twice must not stack a second filter — nor go quiet on the second show.
    menu->popup(moreBtn->mapToGlobal(moreBtn->rect().bottomLeft()));
    QVERIFY(QTest::qWaitForWindowExposed(menu));
    menu->hide();
    QTRY_VERIFY(!menu->isVisible());
    menu->popup(moreBtn->mapToGlobal(moreBtn->rect().bottomLeft()));
    QTRY_VERIFY(menu->isVisible());
    menu->hide();
    QTRY_VERIFY(!menu->isVisible());
    QCOMPARE(menu->findChildren<QObject*>(QStringLiteral("stencilMenuFlight"),
                                          Qt::FindDirectChildrenOnly).size(), 1);

    // …and the window that Settings raises rides the trigger's point — this half DOES
    // fly offscreen, so it is asserted for real.
    QDialog dlg(&win);
    dlg.resize(320, 240);
    stencil::support::revealDialog(dlg, moreBtn);
    dlg.show();
    QTRY_COMPARE(surfaceFlightTarget(&win), want);
    dlg.close();
    awaitFlights(&win);

    // A shut dock leaves nothing on screen to own the window: it falls from above
    // instead of out of the trigger's stale last position.
    win.chatDock_->setVisible(false);
    QTRY_VERIFY(!moreBtn->isVisible());
    QDialog orphan(&win);
    orphan.resize(320, 240);
    stencil::support::revealDialog(orphan, moreBtn);
    orphan.show();
    settle([&] { return surfaceFlightTarget(&win) != QPoint(-1, -1); }, 50);
    const QPoint above = surfaceFlightTarget(&win);
    if (above != QPoint(-1, -1))
      QVERIFY2(above != want, "a hidden trigger must not keep claiming the flight");
    orphan.close();
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

  // Moving the chat between sides animates: it slides out of the old edge and back in at
  // the new one (the same extent slide the icon's open/close uses). It used to jump.
  void chatPlacementChangeAnimates() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* chat = win.findChild<QAction*>("actChat");
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    QVERIFY(chat && dock);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible() && !dock->isFloating());
    awaitAnim(win.chatAnim_);
    const int settled = dock->width();
    QVERIFY2(settled > 80, "the dock never reached its full width");
    // Ask for the opposite side and watch the extent actually move mid-flight.
    emit static_cast<stencil::gui::ChatDock*>(dock)->dockRequested(Qt::RightDockWidgetArea);
    bool sawCollapse = false;
    for (int i = 0; i < 20 && !sawCollapse; ++i) {
      QTest::qWait(20);
      const int e = dock->isFloating() ? settled
                                       : (win.dockWidgetArea(dock) == Qt::TopDockWidgetArea
                                          || win.dockWidgetArea(dock) == Qt::BottomDockWidgetArea)
                                             ? dock->height() : dock->width();
      if (e < settled / 2) sawCollapse = true;
    }
    QVERIFY2(sawCollapse, "the dock jumped to the new side without sliding out");
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::RightDockWidgetArea);
    QTRY_VERIFY2(dock->width() > settled / 2, "it never grew back at the new edge");
    awaitAnim(win.chatAnim_);

    // …and coming back from FLOAT slides in at the side you picked, rather than
    // appearing at full width (the floating branch used to skip the animation).
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    awaitAnim(win.chatAnim_);
    emit static_cast<stencil::gui::ChatDock*>(dock)->dockRequested(Qt::LeftDockWidgetArea);
    bool sawNarrow = false;
    for (int i = 0; i < 20 && !sawNarrow; ++i) {
      QTest::qWait(15);
      if (!dock->isFloating() && dock->width() < settled / 2) sawNarrow = true;
    }
    QVERIFY2(sawNarrow, "docking from float snapped straight to full width");
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::LeftDockWidgetArea);
    QTRY_VERIFY2(dock->width() > settled / 2, "it never grew in from the float");
  }

  // Docking the chat onto the SAME side as the points panel used to let the two fight
  // over width: the panel would balloon or collapse mid-slide (Qt's dock layout freely
  // redistributing space between two flexible siblings), and that bad width then got
  // captured as the "restore" size, reappearing very wide on the next reopen. Browser
  // parity (layout.css .main-content flex row): the chat overlay only ever eats into the
  // canvas column — the fixed-width panel beside it never moves. Regression for that bug.
  void chatSharingPanelSideKeepsPanelWidthStable() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(win.selPanel_ && win.chatDock_);
    QVERIFY(win.dockWidgetArea(win.selPanel_) == Qt::RightDockWidgetArea);
    QTRY_VERIFY(!win.selPanel_->isHidden());

    // Chat starts docked LEFT by default — open it (an UNRELATED area, so this alone
    // settles QMainWindow's dock layout onto the panel's real natural width, not
    // whatever incidental size it had straight out of construction) and let it settle.
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible() && !win.chatDock_->isFloating());
    awaitAnim(win.chatAnim_);
    const int panelBefore = win.selPanel_->width();
    QVERIFY2(panelBefore > 120, "the points panel never reached its natural width");

    // Now place the chat RIGHT, alongside the points panel, and watch the panel's
    // width through the whole flight.
    emit static_cast<stencil::gui::ChatDock*>(win.chatDock_)->dockRequested(Qt::RightDockWidgetArea);
    int maxSeen = 0, minSeen = win.width();
    QVERIFY2(win.chatAnim_, "the placement change did not animate");
    QElapsedTimer flightClock;
    flightClock.start();
    while (win.chatAnim_ && flightClock.elapsed() < 1500) {   // the flight's own length
      QTest::qWait(15);
      if (win.selPanel_->isHidden()) continue;
      const int w = win.selPanel_->width();
      maxSeen = std::max(maxSeen, w);
      minSeen = std::min(minSeen, w);
    }
    QTRY_COMPARE(win.dockWidgetArea(win.chatDock_), Qt::RightDockWidgetArea);
    awaitAnim(win.chatAnim_);
    // They must land SIDE BY SIDE (same row, chat to the right of the panel) —
    // never stacked vertically (Qt's plain, unsplit addDockWidget default).
    QCOMPARE(win.selPanel_->mapTo(&win, QPoint(0, 0)).y(), win.chatDock_->mapTo(&win, QPoint(0, 0)).y());
    QVERIFY2(win.chatDock_->mapTo(&win, QPoint(0, 0)).x() > win.selPanel_->mapTo(&win, QPoint(0, 0)).x(),
             "chat did not land to the right of the points panel");
    // The panel must never balloon past its pre-share width, nor get squeezed away —
    // the chat's own slide is what should move, not the panel sitting beside it.
    QVERIFY2(maxSeen <= panelBefore + 8,
             qPrintable(QString("points panel widened to %1 (was %2)").arg(maxSeen).arg(panelBefore)));
    QVERIFY2(minSeen >= 100,
             qPrintable(QString("points panel collapsed to %1 mid-slide").arg(minSeen)));

    // Close the chat: the panel should hand its width right back...
    win.actChat_->setChecked(false);
    QTRY_VERIFY(!win.chatDock_->isVisible());
    awaitAnim(win.chatAnim_);
    QVERIFY2(win.selPanel_->width() >= panelBefore - 8,
             qPrintable(QString("panel stayed narrow after chat closed: %1 (was %2)")
                            .arg(win.selPanel_->width()).arg(panelBefore)));

    // ...and reopening the chat (sharing again) must not have baked a bad "restore"
    // width into the panel from the earlier fight — it settles back near its own size,
    // never "very wide".
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    awaitAnim(win.chatAnim_);
    QVERIFY2(win.selPanel_->width() <= panelBefore + 8,
             qPrintable(QString("panel reopened very wide: %1 (was %2)")
                            .arg(win.selPanel_->width()).arg(panelBefore)));
  }

  // The points panel can be HIDDEN (no image loaded, or collapsed by the user) at the
  // moment the chat gets placed onto its side — dockChatTo used to gate its split on the
  // panel being visible right then, so the two were left plain-stacked (Qt's unsplit
  // addDockWidget default: one squashed row above the other). Showing the panel again
  // later never re-split them — it just reappeared squashed under the chat. Regression
  // for that; ensurePanelChatSplit must repair it wherever either dock's visibility flips.
  void chatPlacedWhilePanelHiddenStillSplitsSideBySide() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(win.selPanel_ && win.chatDock_ && win.actPanel_ && win.actChat_);
    QVERIFY(win.dockWidgetArea(win.selPanel_) == Qt::RightDockWidgetArea);
    QTRY_VERIFY(!win.selPanel_->isHidden());

    // Hide the points panel FIRST...
    win.actPanel_->setChecked(false);
    QTRY_VERIFY(win.selPanel_->isHidden());

    // ...then place the (unrelated-side) chat onto the panel's side while it's hidden.
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible() && !win.chatDock_->isFloating());
    emit static_cast<stencil::gui::ChatDock*>(win.chatDock_)->dockRequested(Qt::RightDockWidgetArea);
    QTRY_COMPARE(win.dockWidgetArea(win.chatDock_), Qt::RightDockWidgetArea);
    awaitAnim(win.chatAnim_);

    // Now reveal the panel again — it must come back BESIDE the chat, not squashed
    // underneath it.
    win.actPanel_->setChecked(true);
    QTRY_VERIFY(!win.selPanel_->isHidden());
    // The re-laid-out row, not a guess at how long it takes to arrive.
    QTRY_COMPARE_WITH_TIMEOUT(win.selPanel_->mapTo(&win, QPoint(0, 0)).y(),
                              win.chatDock_->mapTo(&win, QPoint(0, 0)).y(), 1000);
    QVERIFY2(win.chatDock_->mapTo(&win, QPoint(0, 0)).x() > win.selPanel_->mapTo(&win, QPoint(0, 0)).x(),
             "the panel reappeared stacked under the chat instead of beside it");
    // Squashed means SHARING a vertical row with the chat — not "shorter than half
    // the window": the stacked toolbars and the top info dock can leave the whole
    // dock row well under half of it. Side by side, both fill that row.
    QCOMPARE(win.selPanel_->height(), win.chatDock_->height());
    QVERIFY(win.centralWidget());
    QVERIFY2(win.selPanel_->height() == win.centralWidget()->height(),
             "the panel came back with a squashed, shared-row height");
  }

  // A FLOATING chat opens out of the toolbar icon and shrinks back into it, like every
  // dialog. It used to blink in and out — the floating branch skipped animation entirely.
  void floatingChatFliesFromItsIcon() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    QVERIFY(dock);
    win.actChat_->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    awaitAnim(win.chatAnim_);
    QWidget* icon = win.buttonForAction(win.actChat_);
    QVERIFY2(icon && icon->isVisible(), "no chat icon to fly from");
    const QPoint iconPoint = flightPointOf(icon, &win);
    // The flight is a cloud of the dock's own pixels inside the MAIN window, aimed at
    // that icon: gathering out of it on the way in, scattering back into it on the way
    // out. Both are the icon — the DIRECTION is what tells the two apart.
    win.actChat_->setChecked(false);          // close: the window comes apart into the icon
    QTRY_VERIFY(surfaceFlight(&win));
    auto* closing = surfaceFlight(&win);
    QVERIFY2(closing, "closing a floating chat did not animate");
    QCOMPARE(closing->surfaceTarget(), iconPoint);
    QVERIFY2(!closing->gathering(), "the close flight scatters INTO the icon, it does not gather");
    awaitAnim(win.chatAnim_);
    win.actChat_->setChecked(true);           // open: it forms out of the icon
    QTRY_VERIFY(surfaceFlight(&win));
    auto* opening = surfaceFlight(&win);
    QVERIFY2(opening, "opening a floating chat did not animate");
    QCOMPARE(opening->surfaceTarget(), iconPoint);
    QVERIFY2(opening->gathering(), "the open flight gathers OUT of the icon");
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

  // A result that lands with no chat surface to show it must still reach the
  // user: a toast, plus an unread mark on the chat icon. The three gaps this
  // pins — a turn finishing DURING the close slide (the dock stays isVisible()
  // for 260ms), a turn owned by the context-menu panel, and the §3.2/§3.1 chain
  // finishing after the reply was announced — were all silent.
  void chatToastAndUnreadCoverEveryClosedState() {
    MainWindow win(nullptr, false);
    win.resize(1200, 820);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    const auto toast = [&]() -> QWidget* {
      return win.chatToast_ && win.chatToast_->isVisible() ? win.chatToast_ : nullptr;
    };
    // Nothing may mark the icon at all now; the lambda stays to prove it.
    const auto unreadShown = [&] {
      for (QLabel* d : win.findChildren<QLabel*>(QStringLiteral("chatUnreadDot")))
        if (d->isVisible()) return true;
      return false;
    };

    // Closed chat: the predicate says "nobody can see this".
    QVERIFY(!win.chatDock_->isVisible());
    QVERIFY2(win.chatSurfaceHidden(), "a closed chat should count as hidden");
    win.showChatToast(QStringLiteral("Assistant finished — done"), true);
    QVERIFY2(toast(), "no toast with the chat closed");
    QVERIFY2(!unreadShown(), "the toast is the whole notice — nothing is left on the icon");

    // Opening clears the mark, and nothing toasts while the chat is up.
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    QVERIFY2(!unreadShown(), "opening must leave the icon unmarked too");
    QVERIFY2(!win.chatSurfaceHidden(), "an open chat must not count as hidden");

    // ITEM A — mid-close: the dock is still isVisible() during its slide, but a
    // result landing then has nowhere to go, so it counts as hidden.
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    win.actChat_->setChecked(false);
    QVERIFY2(win.chatDock_->isVisible(), "the close should still be animating");
    QVERIFY2(win.chatSurfaceHidden(), "a chat mid-close must count as hidden");
    if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim);
    QTRY_VERIFY(!win.chatDock_->isVisible());

    // ITEM C — the context-menu panel is a chat surface too.
    win.ensureChatMenuPanel();
    win.chatMenuPanel_->setGeometry(20, 20, 340, 620);
    win.chatMenuPanel_->show();
    QTRY_VERIFY2(!win.chatSurfaceHidden(), "a visible menu panel must count as a surface");
    win.chatMenuPanel_->hide();
    QTRY_VERIFY2(win.chatSurfaceHidden(), "a dismissed menu panel leaves nothing to look at");

    // ITEM B — §3.0: settling a turn is not itself an event. Nothing runs after
    // the reply, so the terminal has no news of its own to toast.
    if (win.chatToast_) win.chatToast_->hide();
    win.chatTurnSettled();
    QVERIFY2(!toast(), "the turn terminal must be silent — nothing runs after the reply");
    QVERIFY2(!unreadShown(), "…and it must not mark the icon either");
    beat();
  }

  // The title-bar X leaves the SAME way the toolbar toggle does — a docked chat
  // slides into whichever edge it is docked to, a float flies into the icon —
  // for all four dock areas plus floating. It used to call QWidget::close() and
  // simply blink out. Reopening afterwards still works, with the transcript kept.
  void chatCloseButtonAnimatesFromEveryDockArea() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    MainWindow win(nullptr, false);
    win.resize(1200, 820);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    QToolButton* closeBtn = dock->findChild<QToolButton*>();
    // The X is the last ghost in the title bar; find it by its tooltip.
    closeBtn = nullptr;
    for (QToolButton* b : dock->findChildren<QToolButton*>())
      if (b->toolTip() == QLatin1String("Close assistant")) closeBtn = b;
    QVERIFY2(closeBtn, "no X in the chat title bar");
    win.chatDock_->appendUser(QStringLiteral("kept across the close"));

    // The float's exit is a cloud of its own pixels flown inside the main window, every
    // mote pouring back into the icon.
    const auto flight = [&win] { return surfaceFlight(&win); };
    const auto settle = [&win] { awaitAnim(win.chatAnim_); awaitFlights(&win); };

    struct Area { Qt::DockWidgetArea area; const char* name; };
    const QVector<Area> areas{{Qt::LeftDockWidgetArea, "left"},
                              {Qt::RightDockWidgetArea, "right"},
                              {Qt::TopDockWidgetArea, "top"},
                              {Qt::BottomDockWidgetArea, "bottom"}};
    for (const Area& a : areas) {
      win.addDockWidget(a.area, dock);
      dock->setFloating(false);
      win.actChat_->setChecked(true);
      win.setChatShown(true, false);
      QTRY_VERIFY(dock->isVisible());
      settle();
      QCOMPARE(win.dockWidgetArea(dock), a.area);

      closeBtn->click();
      // Mid-slide: the extent animation is running and it has NOT blinked out.
      QVERIFY2(win.chatAnim_ != nullptr,
               qPrintable(QString("%1: the X closed with no animation").arg(a.name)));
      QVERIFY2(dock->isVisible(),
               qPrintable(QString("%1: the dock vanished before the slide").arg(a.name)));
      // …the slide runs toward that edge: width for left/right, height for top/bottom.
      const bool horiz = a.area == Qt::LeftDockWidgetArea || a.area == Qt::RightDockWidgetArea;
      const auto extent = [&] { return horiz ? dock->width() : dock->height(); };
      const int before = extent();
      // Caught while it is STILL on screen: a blink-out leaves nothing to shrink.
      QTRY_VERIFY2_WITH_TIMEOUT(dock->isVisible() && extent() < before,
                                qPrintable(QString("%1: the %2 never shrank from %3")
                                               .arg(a.name, horiz ? "width" : "height")
                                               .arg(before)), 1500);
      QTRY_VERIFY2_WITH_TIMEOUT(!dock->isVisible(),
                                qPrintable(QString("%1: it never finished closing").arg(a.name)), 3000);
      QVERIFY2(!win.actChat_->isChecked(),
               qPrintable(QString("%1: the toolbar toggle stayed lit").arg(a.name)));
      settle();

      // …and it reopens cleanly, transcript intact.
      win.actChat_->setChecked(true);
      QTRY_VERIFY2(dock->isVisible(), qPrintable(QString("%1: it would not reopen").arg(a.name)));
      settle();
      bool kept = false;
      for (QLabel* l : dock->findChildren<QLabel*>())
        if (l->property("chatBody").toString() == QLatin1String("kept across the close")) kept = true;
      QVERIFY2(kept, qPrintable(QString("%1: the close lost the transcript").arg(a.name)));
    }

    // Floating: the X flies the window into the icon.
    dock->setFloating(true);
    win.actChat_->setChecked(true);
    QTRY_VERIFY(dock->isVisible() && dock->isFloating());
    settle();
    const QRect windowBox(dock->mapToGlobal(QPoint(0, 0)), dock->size());
    closeBtn->click();
    stencil::gui::DisintegrateOverlay* from = nullptr;
    QTRY_VERIFY2((from = flight()) != nullptr, "floating: the X closed with no flight");
    QWidget* icon = win.buttonForAction(win.actChat_);
    QVERIFY(icon);
    QVERIFY2(!from->gathering(), "floating: the X must scatter the window INTO the icon");
    QCOMPARE(from->surfaceTarget(), flightPointOf(icon, &win));
    QTRY_VERIFY2(!dock->isVisible(), "floating: it never finished closing");
    settle();
    win.actChat_->setChecked(true);
    QTRY_VERIFY2(dock->isVisible(), "floating: it would not reopen");
    Q_UNUSED(windowBox);
    beat();
  }

  // Opening the COMPACT chat while one is already on screen is a popover swap: the
  // outgoing shape animates out FIRST — a docked panel slides back into its edge, a
  // floating one flies into the icon — and only then does the compact float reveal.
  // Every route has to do it (right-click on the icon, Alt-hover peek, the toolbar
  // action), from BOTH shapes: the float route used to skip it entirely.
  void compactChatSwapAnimatesFromEveryRoute() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    QVERIFY(dock);
    auto* icon = qobject_cast<QToolButton*>(win.buttonForAction(win.actChat_));

    // The outgoing FLOAT's exit is a cloud of its own pixels flown inside the main
    // window: it SCATTERS, every mote pouring back into the icon.
    const auto flight = [&win] { return surfaceFlight(&win); };
    // Past the whole surface flight, so a cloud from the LAST swap can never be mistaken
    // for the next one's (the gather is the longer of the two clocks).
    const auto flushGhosts = [&win] { awaitAnim(win.chatAnim_); awaitFlights(&win); };

    // Put the chat in `floating` shape with a message in it, ready to be swapped.
    const auto arm = [&](bool floating, const QString& mark) {
      win.setChatShown(false, false);
      win.chatCompactPopover_ = false;
      dock->setFloating(floating);
      win.actChat_->setChecked(true);
      win.setChatShown(true, false);
      QTRY_VERIFY(dock->isVisible());
      QCOMPARE(dock->isFloating(), floating);
      win.chatDock_->appendUser(mark);
      flushGhosts();
    };
    // Assert the swap: outgoing animates (slide for a dock, flight for a float),
    // the compact float lands, and the transcript came along.
    const auto expectSwap = [&](bool wasFloating, const QString& mark, const char* route) {
      if (wasFloating) {
        auto* from = flight();
        QVERIFY2(from, qPrintable(QString("%1: the outgoing FLOAT did not fly out").arg(route)));
        QVERIFY2(!from->gathering(),
                 qPrintable(QString("%1: the outgoing float must come APART, not form").arg(route)));
        QCOMPARE(from->surfaceTarget(), flightPointOf(icon, &win));
      } else {
        QVERIFY2(win.chatAnim_ != nullptr,
                 qPrintable(QString("%1: the docked panel did not slide out").arg(route)));
        QVERIFY2(!dock->isFloating(),
                 qPrintable(QString("%1: it tore off before the slide played").arg(route)));
      }
      QTRY_VERIFY_WITH_TIMEOUT(win.chatCompactShowing(), 4000);
      bool kept = false;
      for (QLabel* l : dock->findChildren<QLabel*>())
        if (l->property("chatBody").toString() == mark) kept = true;
      QVERIFY2(kept, qPrintable(QString("%1: the swap lost the conversation").arg(route)));
      flushGhosts();
    };

    // ── the four routes ──
    for (const bool floating : {false, true}) {
      const QString shape = floating ? QStringLiteral("float") : QStringLiteral("dock");
      // Right-click on the toolbar icon (the popover gesture).
      {
        const QString mark = shape + " ctx";
        arm(floating, mark);
        QContextMenuEvent ev(QContextMenuEvent::Mouse, QPoint(4, 4),
                             icon->mapToGlobal(QPoint(4, 4)));
        QApplication::sendEvent(icon, &ev);
        expectSwap(floating, mark, qPrintable(shape + " + right-click"));
      }
      // Alt-hover peek onto the same icon.
      {
        const QString mark = shape + " peek";
        arm(floating, mark);
        win.altPeekOpen(icon, win.actChat_);
        expectSwap(floating, mark, qPrintable(shape + " + alt-peek"));
      }
    }
    // …and the shape the user actually had: a float that IS the compact popover,
    // MOVED away from its anchor. Re-opening it used to teleport the window with
    // no motion at either end — the reported "the chat just vanished".
    {
      QVERIFY(win.chatCompactShowing());
      win.chatDock_->appendUser(QStringLiteral("moved compact"));
      dock->move(dock->pos() + QPoint(160, 120));   // as if dragged
      flushGhosts();
      QContextMenuEvent ev(QContextMenuEvent::Mouse, QPoint(4, 4),
                           icon->mapToGlobal(QPoint(4, 4)));
      QApplication::sendEvent(icon, &ev);
      expectSwap(/*wasFloating=*/true, QStringLiteral("moved compact"),
                 "moved compact float + right-click");
      // Back at the anchor, the same gesture is idempotent: no flight, no move.
      const QRect settled = dock->geometry();
      QApplication::sendEvent(icon, &ev);
      QCOMPARE(dock->geometry(), settled);
      QVERIFY2(!flight(), "a re-pin that moves nothing must not animate");
      QVERIFY(win.chatCompactShowing());
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

  // The REAL input path (no probes): the floating dock's title bar consumes
  // press/move/release itself, so a drag works even where Qt hands the window to the
  // window server (macOS) and never delivers the release.
  void chatDockDragViaMouseEvents() {
    MainWindow win;
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(dock && chat);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    QWidget* title = dock->titleBarWidget();
    QVERIFY(title);
    settleLayout(&win, 60);

    const QRect central(win.centralWidget()->mapTo(&win, QPoint(0, 0)),
                        win.centralWidget()->size());
    const auto sendMouse = [&](QEvent::Type t, const QPoint& global) {
      QMouseEvent e(t, title->mapFromGlobal(global), QPointF(global),
                    t == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                    t == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                    Qt::NoModifier);
      QApplication::sendEvent(title, &e);
    };
    const QPoint start = title->mapToGlobal(QPoint(30, 8));
    sendMouse(QEvent::MouseButtonPress, start);
    sendMouse(QEvent::MouseMove, start + QPoint(40, 40));   // past the threshold
    QTRY_VERIFY2(win.findChild<QWidget*>("chatDockZones"), "a real drag never showed the zones");
    QWidget* zones = win.findChild<QWidget*>("chatDockZones");
    QVERIFY2(zones && zones->isVisible(), "zones show for a real (event-driven) drag");

    // Release inside the RIGHT band → docked right, zones gone.
    const QRect zr = zones->geometry();   // bands span the dock region, not `central`
    const QPoint rightBand = win.mapToGlobal(QPoint(zr.right() - 20, zr.center().y()));
    sendMouse(QEvent::MouseMove, rightBand);
    sendMouse(QEvent::MouseButtonRelease, rightBand);
    QTRY_VERIFY(!dock->isFloating());
    QCOMPARE(win.dockWidgetArea(dock), Qt::RightDockWidgetArea);
    QTRY_VERIFY(!zones->isVisible());
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

  // The placement button matching the current state is accent-marked and inert.
  void chatDockPlacementState() {
    MainWindow win;
    win.resize(1000, 720);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(dock && chat);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    QWidget* title = dock->titleBarWidget();
    QVERIFY(title);
    const QList<QToolButton*> btns = title->findChildren<QToolButton*>();
    QVERIFY(btns.size() >= 6);   // 4 placements + float + close
    // Docked LEFT by default: exactly one placement button wears the active chip. (It is
    // marked by its stylesheet, not by being disabled — a disabled button would be
    // repainted by QToolButton:disabled as a dead bordered square.)
    const auto activeCount = [&] {
      int n = 0;
      for (QToolButton* b : btns)
        if (b->styleSheet().contains("background:")) ++n;
      return n;
    };
    QTRY_COMPARE(activeCount(), 1);
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    QTRY_COMPARE(activeCount(), 1);   // now it's the float button
  }

  void chatDockDragZones() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* chat = win.findChild<QAction*>("actChat");
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    QVERIFY(chat && dock);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    QWidget* title = dock->titleBarWidget();
    QVERIFY(title);

    settleLayout(&win, 60);   // let the layout reclaim the floated dock's slot
    const QRect central(win.centralWidget()->mapTo(&win, QPoint(0, 0)),
                        win.centralWidget()->size());
    // Offscreen has no movable cursor / synthetic global button state, so the
    // poll reads STUBBED probes — exactly the delivery-free situation of a
    // native macOS drag, where moves/releases never reach the widget.
    QPoint stubCursor = win.mapToGlobal(central.topLeft());  // ≠ the first drag target
    bool stubDown = false;
    win.chatDock_->setDragProbesForTest([&stubCursor] { return stubCursor; },
                                        [&stubDown] { return stubDown; });
    const auto dragTo = [&](const QPoint& globalPos) {
      stubDown = true;
      // Synthesized press on the title bar starts the poll (delivery of the
      // PRESS is all the real flow needs — moves/releases are polled).
      QMouseEvent press(QEvent::MouseButtonPress, QPointF(8, 8), QPointF(8, 8),
                        QPointF(title->mapToGlobal(QPoint(8, 8))), Qt::LeftButton,
                        Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(title, &press);
      stubCursor = globalPos;  // observed by the poll loop
    };
    const auto releaseAt = [&](const QPoint& globalPos) {
      stubCursor = globalPos;
      QTest::qWait(40);  // a couple of poll ticks at the drop spot
      stubDown = false;  // "button up" → the poll finishes at stubCursor
      QTest::qWait(40);
    };

    // The overlay appears for the whole drag and spans the DOCK REGION: full
    // window width, below the toolbars, above the status bar — NOT the central
    // widget (which shrinks by whatever is docked, drifting the bands inward).
    dragTo(win.mapToGlobal(central.center()));
    QTRY_VERIFY(win.findChild<QWidget*>("chatDockZones"));
    auto* zones = win.findChild<QWidget*>("chatDockZones");
    QVERIFY(zones);
    QTRY_VERIFY(zones->isVisible());
    const QRect zr = zones->geometry();
    QCOMPARE(zr.left(), 0);
    QCOMPARE(zr.width(), win.width());
    QVERIFY2(zr.top() > 0 && zr.top() <= central.top(), "starts below the toolbars");
    QVERIFY2(zr.bottom() >= central.bottom(), "reaches past the central area's bottom");
    // Native docking is locked out for the whole drag: the zones are the ONLY
    // docking mechanism (Qt can't show its placeholder or hover-dock).
    QCOMPARE(dock->allowedAreas(), Qt::NoDockWidgetArea);

    // LEFT band → docks left; areas restored on release.
    releaseAt(win.mapToGlobal(QPoint(zr.left() + 30, zr.center().y())));
    QTRY_VERIFY(!zones->isVisible());
    QTRY_VERIFY(!dock->isFloating());
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::LeftDockWidgetArea);
    QCOMPARE(dock->allowedAreas(), Qt::AllDockWidgetAreas);

    // Tear-off-from-DOCKED: the drag starts docked, the poll forces the float
    // past the drag threshold (native docking suppressed throughout), the
    // zones appear, and the RIGHT release band decides.
    QVERIFY(!dock->isFloating());
    dragTo(win.mapToGlobal(central.center()));  // press on the DOCKED title
    QTRY_VERIFY(dock->isFloating());            // forced into the zone flow
    QTRY_VERIFY(zones->isVisible());
    QCOMPARE(dock->allowedAreas(), Qt::NoDockWidgetArea);
    releaseAt(win.mapToGlobal(
        QPoint(zr.right() - 30, zr.center().y())));
    QTRY_VERIFY(!zones->isVisible());
    QTRY_VERIFY(!dock->isFloating());
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::RightDockWidgetArea);
    QCOMPARE(dock->allowedAreas(), Qt::AllDockWidgetAreas);

    // BOTTOM band → docks bottom.
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    dragTo(win.mapToGlobal(central.center()));
    QTRY_VERIFY(zones->isVisible());
    releaseAt(win.mapToGlobal(
        QPoint(zr.center().x(), zr.bottom() - 30)));
    QTRY_VERIFY(!zones->isVisible());
    QTRY_VERIFY(!dock->isFloating());
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::BottomDockWidgetArea);

    // Mid-area release → stays floating; the overlay is gone either way.
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    dragTo(win.mapToGlobal(central.center() + QPoint(40, 0)));
    QTRY_VERIFY(zones->isVisible());
    releaseAt(win.mapToGlobal(central.center()));
    QTRY_VERIFY(!zones->isVisible());
    QVERIFY(dock->isFloating());

    // Restore the default placement for later slots.
    win.addDockWidget(Qt::LeftDockWidgetArea, dock);
    dock->setFloating(false);
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    beat();
  }

  // The chat's icon controls shimmer on hover like every other button in the app
  // (browser layout.css shimmers every <button>, chat ones included): the sweep
  // starts on Enter, stops on Leave, and never runs on a disabled control.
  // Checked on the dock's composer + title bar, the per-message "…", and the
  // context-menu panel's composer.
  void chatIconButtonsShimmerOnHover() {
    const auto motion = withMotion();   // the sweep honours motionReduced(), which is on here
    MainWindow win(nullptr, false);
    win.resize(1100, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    win.chatDock_->appendUser(QStringLiteral("shimmer me"));
    win.ensureChatMenuPanel();
    win.chatMenuPanel_->setGeometry(20, 20, 340, 640);
    win.chatMenuPanel_->show();
    win.chatMirror(QStringLiteral("You"), QStringLiteral("shimmer me too"), false);
    settleLayout(win.chatMenuPanel_, 200);

    const auto overlayOf = [](QWidget* w) {
      return w ? w->findChild<QWidget*>("shimmerOverlay") : nullptr;
    };
    const auto hoverEnter = [](QWidget* w) {
      QEnterEvent e(QPointF(4, 4), QPointF(4, 4), w->mapToGlobal(QPoint(4, 4)));
      QApplication::sendEvent(w, &e);
    };
    // Every chat icon control carries the overlay, mouse-through and exactly the
    // size of the button, and a hover starts a sweep that a leave cancels.
    // The overlay is there, sized to the button and mouse-through.
    const auto wired = [&](QWidget* b, const char* what) {
      QWidget* fx = overlayOf(b);
      QVERIFY2(fx, qPrintable(QString("%1: no shimmer overlay").arg(what)));
      QCOMPARE(fx->geometry(), b->rect());
      QVERIFY2(fx->testAttribute(Qt::WA_TransparentForMouseEvents),
               qPrintable(QString("%1: the overlay would eat clicks").arg(what)));
    };
    // …and a hover sweeps it, a leave cancels at once.
    const auto sweeps = [&](QWidget* b, const char* what) {
      wired(b, what);
      QWidget* fx = overlayOf(b);
      QVERIFY(fx);
      hoverEnter(b);
      QTRY_VERIFY2(fx->property("sweepProgress").toReal() >= 0.0,
                   qPrintable(QString("%1: hover started no sweep").arg(what)));
      QEvent leave(QEvent::Leave);
      QApplication::sendEvent(b, &leave);
      QCOMPARE(fx->property("sweepProgress").toReal(), -1.0);   // cancelled at once
    };

    // An ENABLED dock control (the composer's send is disabled on an empty box —
    // it is the disabled case below).
    QToolButton* live = nullptr;
    for (QToolButton* b : win.chatDock_->findChildren<QToolButton*>())
      if (b->isVisible() && b->isEnabled() && overlayOf(b)) { live = b; break; }
    QVERIFY2(live, "no shimmered enabled button in the chat dock");
    sweeps(live, "dock composer/header button");

    QFrame* card = nullptr;
    for (QFrame* f : win.chatDock_->findChildren<QFrame*>("chatCardUser")) card = f;
    QVERIFY(card);
    auto* more = qobject_cast<QToolButton*>(card->property("chatMoreBtn").value<QObject*>());
    QVERIFY2(more, "the card has no \"…\"");
    more->show();   // normally revealed by the card's own hover
    // Presence only: the "…" LIFTS itself 1px on hover, and offscreen QPA (which
    // has no real cursor to keep inside the moved button) answers that move with
    // a synthetic Leave that cancels the sweep. A real pointer stays inside it.
    wired(more, "row-menu \"…\"");

    QToolButton* panelBtn = nullptr;
    for (QToolButton* b : win.chatMenuPanel_->findChildren<QToolButton*>())
      if (b->isEnabled() && overlayOf(b)) { panelBtn = b; break; }
    QVERIFY2(panelBtn, "no shimmered button in the menu panel");
    sweeps(panelBtn, "menu panel composer button");

    // A DISABLED control stays quiet: send, with nothing typed.
    QToolButton* send = win.chatDock_->findChild<QToolButton*>("chatSend");
    QVERIFY(send);
    QVERIFY2(!send->isEnabled(), "the empty composer's send should be disabled");
    QWidget* sendFx = overlayOf(send);
    QVERIFY2(sendFx, "the send button lost its shimmer overlay");
    hoverEnter(send);
    QCOMPARE(sendFx->property("sweepProgress").toReal(), -1.0);
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "mainWindow.chatDock.gui.moc"
