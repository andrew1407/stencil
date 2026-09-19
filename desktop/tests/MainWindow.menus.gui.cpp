// MainWindow GUI e2e — The canvas context menu's Assistant submenu: the entry, its
// composer and the conversation it drives without ever dismissing the menu.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindowMenu.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The canvas context menu carries an "Assistant ▸" submenu (browser parity): present
  // only when a provider is configured, driving the SAME pipeline and history as the
  // dock, and never dismissing the menu while you type, send or receive — while the
  // classic submenus still hover-open (the chat owns its own child menu to keep that).
  void contextMenuAssistantSubmenu() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());  // a working image, so a plan has something to hit

    // Hover a submenu parent the way a user does and wait for its child popup.
    // Open a submenu the way a user does. Hover first — that is the path the
    // regression broke — then fall back to the keyboard: QTest's synthetic
    // moves cannot drive a NATIVE popup grab (macOS), so a headed run would
    // otherwise fail on a harness limitation rather than a real one. Either
    // way the assertion is "the child popup opens", which is what regressed.
    auto openSub = [](QMenu* menu, const QString& title) -> QMenu* {
      QAction* parent = nullptr;
      // startsWith, not == : a submenu-opener's own hint text is native-formatted off
      // the action it names (MainWindow.cpp hintTab — "Image Filter\t⌥B" on macOS,
      // "Image Filter\tAlt+B" elsewhere), so a literal platform-specific suffix isn't
      // reliable to match here. "Style" etc. carry no hint at all, so the prefix IS
      // the whole label — startsWith is exact for them too.
      for (QAction* a : menu->actions())
        if (a->text().startsWith(title)) parent = a;
      if (!parent || !parent->menu()) return nullptr;
      // A move to the position the cursor already occupies produces no event at
      // all, and the first move into a freshly popped menu is routinely
      // swallowed — so nudge via a PLAIN row (never a submenu parent, whose
      // child popup would then cover the row we aim at) and retry the pair.
      for (int attempt = 0; attempt < 4 && !parent->menu()->isVisible(); ++attempt) {
        for (QAction* a : menu->actions()) {
          if (a->isSeparator() || a->menu() || !a->isEnabled()) continue;
          QTest::mouseMove(menu, menu->actionGeometry(a).center());
          break;
        }
        QTest::qWait(30);
        QTest::mouseMove(menu, menu->actionGeometry(parent).center());
        settle([&] { return !(!parent->menu()->isVisible()); }, 400);
      }
      if (!parent->menu()->isVisible()) {  // keyboard fallback
        menu->setActiveAction(parent);
        QTest::keyClick(menu, Qt::Key_Right);
        settle([&] { return !(!parent->menu()->isVisible()); }, 1000);
      }
      return parent->menu()->isVisible() ? parent->menu() : nullptr;
    };
    // Keyboard path: make the parent current and press Right — deterministic,
    // and it doubles as the "arrows/Enter still drive the menu" assertion.


    // ── assistant OFF: no Assistant entry at all, nothing even built ──
    bool sawAssistantWhenOff = true, sawNormalAction = false, doubleSeparator = false;
    bool styleOpenedOff = false, filterOpenedOff = false;
    int separatorsOff = 0;
    win.settings_.llmProvider = "none";
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
    QVERIFY2(!win.chatMenuAction_, "the chat panel was built despite provider=none");
    QVERIFY2(styleOpenedOff && filterOpenedOff, "submenus did not open (assistant off)");
    QVERIFY2(!doubleSeparator, "the hidden Assistant entry left a dangling separator");

    // ── assistant ON ──
    win.settings_.llmProvider = "ollama";
    win.settings_.llmBaseUrl = "http://localhost:11434";
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
    win.llmClient_ = std::make_unique<stencil::llm::LlmClient>(&mock);
    // The menu's attach button feeds the DOCK's attachment state; stage one
    // there and the menu-driven turn must carry it (working image + this one).
    QImage att(12, 8, QImage::Format_RGB32);
    att.fill(Qt::green);
    win.chatDock_->addAttachmentImage(att);

    bool styleOpened = false, filterOpened = false, assistantOpened = false;
    bool tooltipByKey = false, twoButtons = false, dotOnMore = false;
    bool assistantBeforeDrawing = false, buttonsMatchDock = false, splitterResized = false;
    bool noSeparatorBelowAssistant = false;
    int separatorsOn = 0;
    QList<int> splitterSizes, dustClocks;
    bool hintGone = false;
    int pillRest = 0, pillHot = 0, handleWidth = 0;
    bool cursorBefore = true, cursorOnHandle = false, cursorAfter = true;
    double pillCenterX = -1, inputCenterX = -1, panelCenterX = -1;
    int chipCount = 0;
    bool chipsShownEmpty = false, chipTextsMatchDock = false, chipPrefilled = false;
    bool chipDidNotSend = false, sendGatedEmpty = false, chipsHiddenAfterSend = true;
    QStringList overflowItems;
    int transcriptCap = 0, postedImages = 0;
    bool typedThrough = false, subAliveAfterSend = false, rootAliveAfterSend = false;
    bool sendWasStop = false, stopSeen = false, stoppedRowSeen = false;
    bool escClosedSub = false;
    int rowsAfterSend = 0;
    QString posted;
    QTimer::singleShot(0, [&] {
      QMenu* menu = findMenu();
      if (!menu) return;
      // The classic submenus keep working with the Assistant entry present:
      // hover-open for two of them, plus the keyboard path (arrows/Right) which
      // must still drive the menu while nothing has focused the chat input.
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
      // The browser's TWO composer buttons, in order: send · "…", with attach and
      // settings folded into the overflow the reachability dot now rides on.
      twoButtons = sendBtn->x() < moreBtn->x() &&
                   !sub->findChild<QToolButton*>("chatMenuAttach") &&
                   !sub->findChild<QToolButton*>("chatMenuGear");
      if (auto* over = moreBtn->findChild<QMenu*>("chatMenuMoreMenu"))
        for (QAction* a : over->actions()) overflowItems << a->text();
      // …and the DOCK's exact look: same 18 px glyphs, same 26 px box, same
      // accent treatment, so the two composers read identically.
      auto* dockSend = win.chatDock_->findChild<QToolButton*>("chatSend");
      buttonsMatchDock = dockSend && sendBtn->iconSize() == dockSend->iconSize() &&
                         sendBtn->size() == dockSend->size() &&
                         sendBtn->property("chatAccent").toBool() &&
                         moreBtn->iconSize() == QSize(20, 20) &&
                         sendBtn->width() == sendBtn->height() &&
                         moreBtn->size() == sendBtn->size() &&
                         sendBtn->width() >= 30;
      // The explanatory line is gone: an empty transcript stays blank, and no
      // label outside the transcript rows explains the panel.
      hintGone = true;
      for (QLabel* l : panel->findChildren<QLabel*>())
        if (l->text().contains("replies are applied")) hintGone = false;
      // Empty state = the DOCK's suggestion chips, same texts, clickable, and
      // they PREFILL the composer rather than sending.
      if (auto* chipBox = panel->findChild<QWidget*>("chatSuggest")) {
        const auto chips = chipBox->findChildren<QPushButton*>("chatSuggestChip");
        chipCount = chips.size();
        chipsShownEmpty = chipBox->isVisible();
        const auto dockChips =
            win.chatDock_->findChildren<QPushButton*>("chatSuggestChip");
        chipTextsMatchDock = dockChips.size() == chips.size();
        for (int i = 0; chipTextsMatchDock && i < chips.size(); ++i)
          if (chips.at(i)->text() != dockChips.at(i)->text()) chipTextsMatchDock = false;
        if (!chips.isEmpty()) {
          chips.first()->click();
          chipPrefilled = input->toPlainText() == QString("Make it sepia");
          chipDidNotSend = chipBox->isVisible();  // prefill only — no message
          input->clear();
        }
      }
      // The pair is ONE set: identical box, only the enabled state differs
      // (send is gated on non-empty input, exactly like the dock's).
      sendGatedEmpty = !sendBtn->isEnabled() && moreBtn->isEnabled() &&
                       sendBtn->size() == moreBtn->size() &&
                       sendBtn->iconSize() == moreBtn->iconSize() &&
                       sendBtn->property("chatAccent").toBool() &&
                       moreBtn->property("chatAccent").toBool();
      // The splitter handle carries the app's pill affordance, theme-coloured,
      // and grows/accents on hover — not Qt's dotted nub.
      if (auto* sp0 = panel->findChild<QSplitter*>("chatMenuSplitter")) {
        if (QWidget* h = sp0->handle(1)) {
          handleWidth = sp0->handleWidth();
          const QImage rest = h->grab().toImage();
          // Width of the painted pill: the widest run of pixels that differ
          // from the handle's own background (sampled at a corner).
          auto pillWidth = [](const QImage& img) {
            if (img.isNull()) return 0;
            const QRgb bg = img.pixel(0, 0);
            int best = 0;
            for (int y = 0; y < img.height(); ++y) {
              int run = 0;
              for (int x = 0; x < img.width(); ++x)
                if (img.pixel(x, y) != bg) ++run;
              best = qMax(best, run);
            }
            // grab() renders at the device pixel ratio — report LOGICAL px so
            // the bounds hold on a Retina display too.
            return qRound(best / img.devicePixelRatio());
          };
          pillRest = pillWidth(rest);
          // Centring, pinned by MEASUREMENT: the pill's painted centre must sit
          // on the INPUT column the drag resizes — not on the handle's full
          // span, which also covers the send/attach/gear cluster (that was the
          // off-centre bug: 187 px vs the input's 138 px).
          {
            const QRgb bg = rest.pixel(0, 0);
            int lo = INT_MAX, hi = -1;
            for (int y = 0; y < rest.height(); ++y)
              for (int x = 0; x < rest.width(); ++x)
                if (rest.pixel(x, y) != bg) { lo = qMin(lo, x); hi = qMax(hi, x); }
            if (hi >= 0) {
              const double dpr = rest.devicePixelRatio();
              const double cxInHandle = (lo / dpr + (hi + 1) / dpr) / 2.0;
              pillCenterX = h->mapTo(panel, QPoint(0, 0)).x() + cxInHandle;
              inputCenterX = input->mapTo(panel, QPoint(0, 0)).x() + input->width() / 2.0;
              panelCenterX = panel->width() / 2.0;
            }
          }
          // Hover it the way a user does — a real move over the menu. The popup
          // grab suppresses the handle's own enter/leave, so the menu has to
          // synthesise them; this asserts that plumbing as well as the pill.
          const QPoint over = h->mapTo(sub, h->rect().center());
          // Real MouseMove events sent to the menu — QTest::mouseMove never
          // reaches a NATIVE popup, so this is the form that exercises the
          // grab path both offscreen and headed.
          const auto moveTo = [sub](const QPoint& p) {
            QMouseEvent e(QEvent::MouseMove, QPointF(p), QPointF(sub->mapToGlobal(p)),
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(sub, &e);
          };
          cursorBefore = QApplication::overrideCursor() != nullptr;
          moveTo(QPoint(over.x(), over.y() - 40));
          QTest::qWait(20);
          moveTo(over);
          QTest::qWait(200);  // the growth animation settles
          pillHot = pillWidth(h->grab().toImage());
          if (QCursor* oc = QApplication::overrideCursor())
            cursorOnHandle = oc->shape() == Qt::SplitVCursor;
          // …and both must revert when the pointer leaves.
          moveTo(QPoint(over.x(), over.y() - 40));
          QTest::qWait(60);
          cursorAfter = QApplication::overrideCursor() != nullptr;
        }
      }
      // Resizable composer: a splitter between transcript and composer, whose
      // drag redistributes a pinned total (the popup stays a sane size).
      if (auto* sp = panel->findChild<QSplitter*>("chatMenuSplitter")) {
        const QList<int> before = sp->sizes();
        sp->setSizes({before.at(0) - 40, before.at(1) + 40});
        const QList<int> after = sp->sizes();
        splitterResized = after.at(1) > before.at(1) &&
                          (after.at(0) + after.at(1)) == (before.at(0) + before.at(1));
        splitterSizes = after;
      }
      auto* dot = sub->findChild<QLabel*>("chatMenuStatusDot");
      dotOnMore = dot && dot->parentWidget() == moreBtn;
      // Room for a reply plus a couple of exchanges without scrolling.
      // The APPLIED height, not the constant: a plain maximumHeight let the
      // scroll area collapse to its content sizeHint (~2 rows).
      if (auto* scrollArea = panel->findChild<QScrollArea*>("chatMenuTranscript"))
        transcriptCap = scrollArea->height();
      QCOMPARE(input->placeholderText(),
               QString("Ask the assistant… (Enter sends, Shift+Enter newline)"));
      // It must not take focus implicitly — menu navigation stays live until
      // the user actually clicks into the input.
      QCOMPARE(input->focusPolicy(), Qt::ClickFocus);

      // Click into the input THROUGH the menu (the real popup path): focus
      // lands there and the submenu does not close.
      QTest::mouseClick(sub, Qt::LeftButton, {},
                        input->mapTo(sub, input->rect().center()));
      // Typing goes to the INPUT, not the menu's key navigation.
      QTest::keyClicks(sub, "make it sepia");
      typedThrough = input->toPlainText() == QString("make it sepia");
      // Enter sends instead of activating the highlighted menu item.
      QTest::keyClick(sub, Qt::Key_Return);
      posted = QString::fromUtf8(QJsonDocument(mock.body).toJson(QJsonDocument::Compact));
      const QJsonArray msgs = mock.body.value("messages").toArray();
      if (!msgs.isEmpty())
        postedImages = msgs.last().toObject().value("images").toArray().size();
      subAliveAfterSend = sub->isVisible();
      rootAliveAfterSend = menu->isVisible();
      rowsAfterSend = panel->findChildren<QLabel*>().size();
      if (auto* chipBox = panel->findChild<QWidget*>("chatSuggest"))
        chipsHiddenAfterSend = !chipBox->isVisible();

      // Busy → the send button becomes STOP; clicking it THROUGH the menu
      // aborts without closing anything.
      win.chatDock_->setBusy(true);
      win.chatMirrorBusy(true);
      win.chatMirrorPending(true);
      sendWasStop = sendBtn->toolTip() == QString("Stop the response");
      QTest::mouseClick(sub, Qt::LeftButton, {},
                        sendBtn->mapTo(sub, sendBtn->rect().center()));
      stopSeen = win.chatStopRequested_ && sub->isVisible() && menu->isVisible();
      // The canceled reply turns the in-flight row into a muted "Stopped.".
      win.chatDock_->setBusy(false);
      win.chatMirrorBusy(false);
      stencil::llm::LlmReply canceled;
      canceled.ok = false;
      canceled.failure = stencil::llm::LlmFailure::TRANSPORT;
      canceled.error = "Operation canceled";
      win.onChatReply(canceled);
      for (QLabel* l : panel->findChildren<QLabel*>())
        if (l->text().contains("Stopped.")) stoppedRowSeen = true;

      // Escape belongs to the menu even with the input focused.
      QTest::keyClick(sub, Qt::Key_Escape);
      escClosedSub = !sub->isVisible();
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
    QVERIFY2(buttonsMatchDock, "the menu composer buttons do not match the dock's");
    QVERIFY2(splitterResized, "the menu composer is not resizable");
    QVERIFY2(hintGone, "the explanatory hint line is still in the submenu");
    QCOMPARE(chipCount, 4);
    QVERIFY2(chipsShownEmpty, "no suggestion chips in the menu's empty state");
    QVERIFY2(chipTextsMatchDock, "the menu chips differ from the dock's");
    QVERIFY2(chipPrefilled, "clicking a chip did not prefill the composer");
    QVERIFY2(chipDidNotSend, "clicking a chip sent instead of prefilling");
    QVERIFY2(chipsHiddenAfterSend, "the chips survived the first message");
    QVERIFY2(sendGatedEmpty,
             "the composer pair is not one set (size/box/accent) with send merely disabled");
    QVERIFY2(handleWidth >= 6, "the splitter handle lost its grab area");
    // A centred pill at rest that GROWS on hover (44 → 68 px by design).
    QVERIFY2(pillRest >= 30 && pillRest <= 60,
             qPrintable(QString("resting pill is %1 px wide").arg(pillRest)));
    QVERIFY2(pillHot > pillRest,
             qPrintable(QString("pill did not grow on hover (%1 → %2)")
                            .arg(pillRest).arg(pillHot)));
    // The resize cursor: none before, SplitVCursor while over the handle, and
    // fully restored (not merely a different shape) once the pointer leaves.
    QVERIFY2(!cursorBefore, "an override cursor was already active");
    QVERIFY2(cursorOnHandle, "hovering the splitter handle showed no resize cursor");
    QVERIFY2(!cursorAfter, "the resize cursor leaked past the splitter handle");
    QVERIFY(pillCenterX >= 0);
    QVERIFY2(qAbs(pillCenterX - inputCenterX) <= 2.0,
             qPrintable(QString("pill centre %1 is off the input centre %2")
                            .arg(pillCenterX).arg(inputCenterX)));
    // Guard the regression itself: the input column is genuinely narrower than
    // the panel, so "centred on the panel" would be a visible miss.
    QVERIFY2(qAbs(panelCenterX - inputCenterX) > 10.0,
             "the input no longer differs from the panel centre — assertion is vacuous");
    QVERIFY2(typedThrough, "keys typed at the menu never reached the chat input");
    QVERIFY2(!posted.isEmpty() && posted.contains("make it sepia"),
             "Enter in the menu did not send through the shared LLM client");
    QVERIFY2(subAliveAfterSend && rootAliveAfterSend, "the menu closed on send");
    QVERIFY2(rowsAfterSend >= 2, "the menu transcript did not record the exchange");
    QVERIFY2(sendWasStop, "the menu send button did not become STOP while busy");
    QVERIFY2(stopSeen, "clicking STOP in the menu closed it or did not abort");
    QVERIFY2(stoppedRowSeen, "the canceled turn did not render as Stopped.");
    QVERIFY2(escClosedSub, "Escape did not close the assistant submenu");
    QCOMPARE(dustClocks, QList<int>(2, stencil::support::CONTEXT_MENU_DUST_MS));
    QVERIFY2(twoButtons, "the menu composer is not the browser's send + \"…\" pair");
    QCOMPARE(overflowItems, QStringList({"Add image", "Swap message sides", "Settings"}));
    QVERIFY2(dotOnMore, "the provider status dot is not on the menu's \"…\"");
    QVERIFY2(transcriptCap >= 140,
             qPrintable(QString("the menu transcript renders only %1 px tall")
                            .arg(transcriptCap)));
    // Working image + its §7 edge map + the attachment staged on the dock: the
    // menu send goes through the same attachment state the attach button feeds.
    QCOMPARE(postedImages, 3);

    // ONE conversation: the turn typed in the menu is in the shared history AND
    // rendered in the dock.
    QCOMPARE(win.chatHistory_.size(), 2);  // user + assistant
    QCOMPARE(win.chatHistory_.first().text, QString("make it sepia"));
    bool dockSawIt = false;
    for (QLabel* l : win.chatDock_->findChildren<QLabel*>())
      if (l->text().contains("make it sepia")) dockSawIt = true;
    QVERIFY2(dockSawIt, "the menu turn never reached the dock transcript");

    // The panel is a QWidgetAction owned by the WINDOW, so reopening the menu
    // (rebuilt from scratch on every right-click) keeps the transcript.
    bool survived = false;
    QTimer::singleShot(0, [&] {
      QMenu* menu = findMenu();
      if (!menu) return;
      QMenu* sub = openSubByKey(menu, "Assistant");
      if (sub)
        if (auto* panel = sub->findChild<QWidget*>("chatMenuPanel"))
          for (QLabel* l : panel->findChildren<QLabel*>())
            if (l->text().contains("make it sepia")) survived = true;
      menu->close();
    });
    win.showContextMenu(win.mapToGlobal(QPoint(400, 300)));
    QVERIFY2(survived, "reopening the menu lost the chat transcript");
    // The panel outlives the menu, so the composer size the user dragged to
    // persists for the session.
    if (auto* sp = win.chatMenuPanel_->findChild<QSplitter*>("chatMenuSplitter"))
      QCOMPARE(sp->sizes(), splitterSizes);

    // The dock's trash clears both surfaces.
    win.onChatClear();
    QVERIFY(win.chatHistory_.isEmpty());
    QVERIFY(win.chatMenuPanel_);
    // The mirrored rows scatter and then go (dock parity), so they leave on the
    // event loop — a hidden row is already on its way out and doesn't count.
    const auto menuRowsLeft = [&win] {
      for (QLabel* l : win.chatMenuPanel_->findChildren<QLabel*>())
        if (!l->isHidden() && l->text().contains("make it sepia")) return true;
      return false;
    };
    QTRY_VERIFY2(!menuRowsLeft(), "clearing the conversation left the menu transcript");

    win.llmClient_.reset();  // drop the mock before it goes out of scope
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.menus.gui.moc"
