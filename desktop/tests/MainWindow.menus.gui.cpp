// MainWindow GUI e2e — Context menus, submenus and the export option popups, plus the Alt-hold
// peek and the hotkey chips their rows wear.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

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
    auto openSubByKey = [](QMenu* menu, const QString& title) -> QMenu* {
      QAction* parent = nullptr;
      for (QAction* a : menu->actions())    // startsWith — see openSub's comment above
        if (a->text().startsWith(title)) parent = a;
      if (!parent || !parent->menu()) return nullptr;
      menu->setActiveAction(parent);
      QTest::keyClick(menu, Qt::Key_Right);
      settle([&] { return !(!parent->menu()->isVisible()); }, 1000);
      return parent->menu()->isVisible() ? parent->menu() : nullptr;
    };
    auto findMenu = []() -> QMenu* {
      QMenu* menu = nullptr;
      for (int i = 0; i < 200 && !menu; ++i) {
        menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) QTest::qWait(10);
      }
      return menu;
    };

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
    bool tooltipByKey = false, threeButtons = false, dotOnGear = false;
    bool assistantBeforeDrawing = false, buttonsMatchDock = false, splitterResized = false;
    bool noSeparatorBelowAssistant = false;
    int separatorsOn = 0;
    QList<int> splitterSizes;
    bool hintGone = false;
    int pillRest = 0, pillHot = 0, handleWidth = 0;
    bool cursorBefore = true, cursorOnHandle = false, cursorAfter = true;
    double pillCenterX = -1, inputCenterX = -1, panelCenterX = -1;
    int chipCount = 0;
    bool chipsShownEmpty = false, chipTextsMatchDock = false, chipPrefilled = false;
    bool chipDidNotSend = false, sendGatedEmpty = false, chipsHiddenAfterSend = true;
    bool attachFrozen = false;
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
      auto* attachBtn = sub->findChild<QToolButton*>("chatMenuAttach");
      auto* gearBtn = sub->findChild<QToolButton*>("chatMenuGear");
      if (!panel || !input || !sendBtn || !attachBtn || !gearBtn) { menu->close(); return; }
      // The dock's three composer buttons, same order: send · attach · gear,
      // with the reachability dot riding on the gear.
      threeButtons = sendBtn->x() < attachBtn->x() && attachBtn->x() < gearBtn->x();
      // …and the DOCK's exact look: same 18 px glyphs, same 26 px box, same
      // accent treatment, so the two composers read identically.
      auto* dockSend = win.chatDock_->findChild<QToolButton*>("chatSend");
      buttonsMatchDock = dockSend && sendBtn->iconSize() == dockSend->iconSize() &&
                         sendBtn->size() == dockSend->size() &&
                         sendBtn->property("chatAccent").toBool() &&
                         attachBtn->iconSize() == QSize(20, 20) &&
                         gearBtn->iconSize() == QSize(20, 20) &&
                         sendBtn->width() == sendBtn->height() &&
                         attachBtn->size() == sendBtn->size() &&
                         gearBtn->size() == sendBtn->size() &&
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
      // The trio is ONE set: identical box, only the enabled state differs
      // (send is gated on non-empty input, exactly like the dock's).
      sendGatedEmpty = !sendBtn->isEnabled() && attachBtn->isEnabled() &&
                       gearBtn->isEnabled() &&
                       sendBtn->size() == attachBtn->size() &&
                       attachBtn->size() == gearBtn->size() &&
                       sendBtn->iconSize() == attachBtn->iconSize() &&
                       attachBtn->iconSize() == gearBtn->iconSize() &&
                       sendBtn->property("chatAccent").toBool() &&
                       attachBtn->property("chatAccent").toBool() &&
                       gearBtn->property("chatAccent").toBool();
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
      dotOnGear = dot && dot->parentWidget() == gearBtn;
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
      attachFrozen = !attachBtn->isEnabled();  // frozen mid-turn, like the dock
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
             "the composer trio is not one set (size/box/accent) with send merely disabled");
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
    QVERIFY2(threeButtons, "the menu composer lost the send/attach/gear trio");
    QVERIFY2(dotOnGear, "the provider status dot is not on the menu gear");
    QVERIFY2(attachFrozen, "attach stayed live during an in-flight turn");
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

  // A context submenu hover-opens, SubmenuCloseGuard closes it again on a hover-away, and
  // on a real display it dusts on every open — including the second open of the same QMenu
  // instance, which is what MenuReveal's re-arming fixed. The dust half cannot run here:
  // isDustMotionOk() refuses on the `offscreen` platform, which nothing can lift, so only the
  // open/close half is asserted and the case reports itself SKIPPED.
  void ctxSubmenuDustReplayProbe() {
    const auto motion = withMotion();
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());

    const auto moveTo = [](QMenu* m, const QPoint& p) {
      QMouseEvent e(QEvent::MouseMove, QPointF(p), QPointF(m->mapToGlobal(p)),
                    Qt::NoButton, Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(m, &e);
    };
    const auto hoverPath = [&](QMenu* m, const QPoint& from, const QPoint& to) {
      for (int i = 1; i <= 8; ++i) { moveTo(m, from + (to - from) * i / 8); QTest::qWait(15); }
    };
    const auto dustSeen = [&win] {
      for (QWidget* w : win.findChildren<QWidget*>(
               QString::fromLatin1(stencil::gui::DisintegrateOverlay::OBJECT_NAME))) {
        auto* fx = static_cast<stencil::gui::DisintegrateOverlay*>(w);
        if (fx->surfacePicture().isValid()) return true;
      }
      return false;
    };

    bool dustOnFirstOpen = false, dustOnClose = false, closed = false, dustOnSecondOpen = false;
    QTimer::singleShot(0, [&] {
      QMenu* menu = nullptr;
      for (int i = 0; i < 200 && !menu; ++i) {
        menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) QTest::qWait(10);
      }
      if (!menu) return;
      QAction* parent = nullptr;
      for (QAction* a : menu->actions())
        if (a->text().startsWith("Image / Layout")) parent = a;
      if (!parent || !parent->menu()) return;
      QAction* plainRow = nullptr;
      for (QAction* a : menu->actions()) {
        if (a->isSeparator() || a->menu() || !a->isEnabled()) continue;
        plainRow = a; break;
      }
      if (!plainRow) return;
      const QPoint plainCenter = menu->actionGeometry(plainRow).center();
      const QPoint parentCenter = menu->actionGeometry(parent).center();
      QMenu* sub = parent->menu();

      // Open #1.
      for (int attempt = 0; attempt < 4 && !sub->isVisible(); ++attempt) {
        moveTo(menu, plainCenter); QTest::qWait(30);
        moveTo(menu, parentCenter);
        settle([&] { return sub->isVisible(); }, 400);
      }
      if (!sub->isVisible()) { menu->close(); return; }
      dustOnFirstOpen = dustSeen();

      // Hover away — our own SubmenuCloseGuard should hide it AND dust it.
      hoverPath(menu, parentCenter, plainCenter);
      for (int i = 0; i < 60 && sub->isVisible(); ++i) { QTest::qWait(10); if (dustSeen()) dustOnClose = true; }
      // The flight is spawned by the hide itself (menuReveal.cpp dustMenuOut off
      // aboutToHide), so it is only there to see once the popup has gone.
      if (dustSeen()) dustOnClose = true;
      closed = !sub->isVisible();

      // Open #2 — the SAME QMenu instance, reopened.
      hoverPath(menu, plainCenter, parentCenter);
      settle([&] { return sub->isVisible(); }, 600);
      dustOnSecondOpen = dustSeen();

      menu->close();
    });
    win.showContextMenu(win.mapToGlobal(QPoint(400, 300)));

    // Correctness first, and it holds on every platform: our guard really does close a
    // hovered-away submenu (Qt itself leaves it up).
    QVERIFY2(closed, "the submenu never closed");
    if (!stencil::support::isDustMotionOk())
      QSKIP("dust is gated off on the offscreen platform (isDustMotionOk) — "
            "run this binary on a real display to exercise the flights");
    QVERIFY2(dustOnFirstOpen, "no dust on the first open");
    QVERIFY2(dustOnClose, "no dust while our own guard closed the submenu");
    QVERIFY2(dustOnSecondOpen, "no dust replayed on the second open of the same submenu");
  }

  // A dialog opened from the MENU BAR must know which row it came out of, so
  // support::revealDialog can grow the window from there when the toolbar icon is hidden.
  void menuOpenedDialogRemembersItsRow() {
    MainWindow win;
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // Any dialog action that lives on both a menu and a toolbar icon.
    QAction* act = nullptr;
    QMenu* owner = nullptr;
    for (QMenu* m : win.menuBar()->findChildren<QMenu*>()) {
      for (QAction* a : m->actions())
        if (win.pop_.dialogActions.contains(a) && a->isEnabled()) { act = a; owner = m; break; }
      if (act) break;
    }
    QVERIFY2(act && owner, "no dialog action on the menu bar to test");

    owner->popup(win.mapToGlobal(QPoint(40, 40)));
    QVERIFY(QTest::qWaitForWindowExposed(owner));
    const QRect row = owner->actionGeometry(act);
    QVERIFY(row.isValid());
    // Hover the row the way a user does, then let the menu close and the action fire.
    QTest::mouseMove(owner, row.center());
    QTest::qWait(30);
    QVERIFY2(win.pop_.menuRowAction == act, "the hovered row was not recorded");
    const QRect rowGlobal(owner->mapToGlobal(row.topLeft()), row.size());
    QCOMPARE(win.pop_.menuRowRect, rowGlobal);
    // Qt hides the menu and THEN activates the action, in the same pass of the event loop.
    // The record has to still be there at that point — this is exactly what reading
    // QApplication::activePopupWidget() inside triggered() got wrong.
    owner->close();
    QVERIFY2(win.pop_.menuRowAction == act, "the row was forgotten before the action fired");
    // The dialog itself blocks in exec(), so drive only the handler that stamps the anchor.
    win.pop_.dialogAnchorRect = (win.pop_.menuRowAction == act) ? win.pop_.menuRowRect : QRect();
    QCOMPARE(win.pop_.dialogAnchorRect, rowGlobal);
    // …and the record does not linger: the next run from an icon/shortcut is not the menu's.
    QTest::qWait(30);
    QVERIFY2(!win.pop_.menuRowAction, "the hovered row outlived its menu");
  }

  // Shift+F10 (shared hotkeysConfig contextMenu) opens the canvas context menu from the
  // keyboard: under the pointer while it rests over the canvas viewport, else at the
  // viewport's centre — the browser's placement for the same chord.
  void contextMenuOpensOnShiftF10() {
    MainWindow win(nullptr, /*restoreLast=*/false);
    CanvasWidget* canvas = openLoaded(win);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->hasImage(), 5000);
    QVERIFY(win.actContextMenu_);
    QCOMPARE(win.actContextMenu_->shortcut(), QKeySequence("Shift+F10"));
    if (QWidget* fw = QApplication::focusWidget()) fw->clearFocus();
    win.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&win));   // a WindowShortcut needs the active window
    QWidget* vp = win.scroll_->viewport();
    const QRect vpGlobal(vp->mapToGlobal(QPoint(0, 0)), vp->size());
    // The menu exec()s: a poll (armed BEFORE the press — the platform key path flushes
    // pending events, so a one-shot would fire too early) records where it opened and closes it.
    const auto armCloser = [&win, vp](bool& opened, QPoint& at, QRect& vpAt) {
      auto* poll = new QTimer(&win);
      poll->setInterval(10);
      int ticks = 0;
      QObject::connect(poll, &QTimer::timeout, &win, [poll, vp, &opened, &at, &vpAt, ticks]() mutable {
        if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
          opened = true;
          at = menu->pos();
          vpAt = QRect(vp->mapToGlobal(QPoint(0, 0)), vp->size());
          menu->close();
          poll->stop();
          poll->deleteLater();
        } else if (++ticks > 300) {
          poll->stop();
          poll->deleteLater();
        }
      });
      poll->start();
    };
    // Pointer resting on the canvas: the menu grows from right there — via the chord
    // itself, through the platform window so the press walks the real shortcut map.
    const QPoint onCanvas = vpGlobal.topLeft() + QPoint(40, 40);
    QCursor::setPos(onCanvas);
    QPoint at1(-1, -1);
    QRect vpAt1;
    bool opened1 = false;
    armCloser(opened1, at1, vpAt1);
    QTest::keyClick(win.windowHandle(), Qt::Key_F10, Qt::ShiftModifier);
    QTRY_VERIFY2_WITH_TIMEOUT(opened1, "Shift+F10 did not open the canvas context menu", 4000);
    // x is the pointer's; y may be pulled up to keep the menu on the (short) offscreen screen.
    QVERIFY2(vpAt1.contains(onCanvas), "the pointer was not over the viewport after all");
    QCOMPARE(at1.x(), onCanvas.x());
    QVERIFY(at1.y() <= onCanvas.y());
    QTRY_VERIFY(!QApplication::activePopupWidget());
    // Pointer off the canvas (on the toolbar): the menu lands at the viewport's centre.
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() - 8, 8)));
    QPoint at2(-1, -1);
    QRect vpAt2;
    bool opened2 = false;
    armCloser(opened2, at2, vpAt2);
    win.actContextMenu_->trigger();
    QTRY_VERIFY2_WITH_TIMEOUT(opened2, "the context-menu action did not open the menu", 4000);
    // Against the viewport as it was AT THAT INSTANT: the panel settles into its width
    // after the window opens, so a rect read before the trigger is a different centre.
    // x lands on the centre exactly; y may be pulled up to keep the menu on screen.
    QCOMPARE(at2.x(), vpAt2.center().x());
    QVERIFY(at2.y() <= vpAt2.center().y());
    QTRY_VERIFY(!QApplication::activePopupWidget());
  }


  // The context menu must open anywhere on the canvas SURFACE, not only on the
  // image. The canvas widget is sized to the image, so the backdrop around a
  // zoomed-out image belongs to the scroll area's viewport — which had no menu at
  // all. With NO image there is no menu anywhere: every entry acts on an image
  // (browser contextMenu.js parity).
  void contextMenuOpensOnEmptyCanvasArea() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QWidget* viewport = win.findChild<QScrollArea*>()->viewport();
    QVERIFY(viewport);

    // Right-click a corner of the canvas area — clearly outside any image.
    auto rightClickCorner = [&win, viewport](bool* opened, bool* copyEnabled,
                                             bool* copyFound) {
      QTimer::singleShot(0, [&win, opened, copyEnabled, copyFound] {
        QMenu* menu = nullptr;
        for (int i = 0; i < 200 && !menu; ++i) {
          menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
          if (!menu) QTest::qWait(10);
        }
        if (!menu) return;
        *opened = true;
        // The "Current" copy-image variant action — syncContextActions has just
        // run for this popup. actCopyImage_ is a fixed pointer (not text-matched):
        // its own text no longer starts with "Copy Image" now that it is nested
        // under a "Copy Image ▸" submenu parent (which is a DIFFERENT, always-
        // enabled QAction — the submenu opener, not the image-dependent copy itself).
        *copyFound = win.actCopyImage_ != nullptr;
        *copyEnabled = win.actCopyImage_ && win.actCopyImage_->isEnabled();
        menu->close();
      });
      const QPoint corner(6, 6);
      QTest::mouseClick(viewport, Qt::RightButton, {}, corner);
      QTest::qWait(50);
    };

    // ── no image at all: NO menu — a popup of dead rows is worse than none. Clicked
    // directly (not through rightClickCorner): its poll would spin for two seconds
    // waiting for a menu that never comes, and still be running for the next case.
    QVERIFY(!win.findChild<CanvasWidget*>()->hasImage());
    QTest::mouseClick(viewport, Qt::RightButton, {}, QPoint(6, 6));
    QTest::qWait(50);
    QVERIFY2(!QApplication::activePopupWidget(), "the context menu opened with no image");
    // …and the keyboard route (Shift+F10) goes through the same gate.
    win.showContextMenuFromKeyboard();
    QTest::qWait(30);
    QVERIFY2(!QApplication::activePopupWidget(), "Shift+F10 opened a menu with no image");

    // ── with an image loaded, clicking OUTSIDE it (the backdrop) ──
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    bool openedOutside = false, copyEnabledOutside = false, copyFoundOutside = false;
    rightClickCorner(&openedOutside, &copyEnabledOutside, &copyFoundOutside);
    QVERIFY2(openedOutside, "no context menu on the backdrop around the image");
    QVERIFY2(copyEnabledOutside, "image actions stayed disabled with an image loaded");
    beat();
  }

  // The copy/download-image toolbar buttons open a small variant-options popup on
  // right-click instead of re-running the plain action (browser parity:
  // js/ui/exportOptionsMenu.js) — verifies wireExportOptionsPopups(). The copy button's
  // plain click is also exercised (safe: no blocking dialog, unlike Save's file picker).
  void toolbarImageButtonsOpenExportOptionsOnRightClick() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());

    QWidget* saveBtn = win.buttonForAction(win.actSaveImage_);
    QWidget* copyBtn = win.buttonForAction(win.actCopyImage_);
    QVERIFY(saveBtn);
    QVERIFY(copyBtn);
    QVERIFY(win.saveImageOptionsMenu_);
    QVERIFY(win.copyImageOptionsMenu_);
    QVERIFY(win.saveImageOptionsMenu_->actions().contains(win.actSaveImageCurrentRow_));
    QVERIFY(win.saveImageOptionsMenu_->actions().contains(win.actSaveImageOriginal_));
    QVERIFY(win.saveImageOptionsMenu_->actions().contains(win.actSaveImageTint_));
    QVERIFY(win.copyImageOptionsMenu_->actions().contains(win.actCopyImageCurrentRow_));
    QVERIFY(win.copyImageOptionsMenu_->actions().contains(win.actCopyImageOriginal_));
    QVERIFY(win.copyImageOptionsMenu_->actions().contains(win.actCopyImageTint_));

    // Right-click the Download button: the popup opens, the plain action does NOT fire
    // (a real download would pop a blocking file dialog — this must never happen here).
    int saveTriggers = 0;
    connect(win.actSaveImage_, &QAction::triggered, &win, [&] { ++saveTriggers; });
    QContextMenuEvent saveCtx(QContextMenuEvent::Mouse, saveBtn->rect().center(),
                              saveBtn->mapToGlobal(saveBtn->rect().center()));
    QApplication::sendEvent(saveBtn, &saveCtx);
    QVERIFY2(QApplication::activePopupWidget() == win.saveImageOptionsMenu_,
             "right-click on the download-image button opened no popup, or the wrong one");
    QCOMPARE(saveTriggers, 0);
    win.saveImageOptionsMenu_->close();

    // Same gesture on the Copy button.
    QContextMenuEvent copyCtx(QContextMenuEvent::Mouse, copyBtn->rect().center(),
                              copyBtn->mapToGlobal(copyBtn->rect().center()));
    QApplication::sendEvent(copyBtn, &copyCtx);
    QVERIFY2(QApplication::activePopupWidget() == win.copyImageOptionsMenu_,
             "right-click on the copy-image button opened no popup, or the wrong one");
    win.copyImageOptionsMenu_->close();

    // A plain single click on Copy still runs the default ("current") variant — deferred
    // briefly (so a following dblclick could still cancel it, though none comes here).
    int copyTriggers = 0;
    connect(win.actCopyImage_, &QAction::triggered, &win, [&] { ++copyTriggers; });
    QTest::mouseClick(copyBtn, Qt::LeftButton);
    QTRY_COMPARE(copyTriggers, 1);
    beat();
  }

  // Holding Alt over an export-variant row peeks its live preview instead of closing the
  // menu: the preview's dust must not span an ESCAPING top-level window while the menu
  // holds the platform grab (menuReveal.cpp's dustMenuIn/dustMenuOut solved the same for a
  // menu's own dust). Three routes reach such a row — the toolbar button's single-level
  // options popup, and the canvas menu's doubly-nested Image/Layout ▸ Copy Image ▸ … chain,
  // where a hover-opened submenu never grabs the keyboard, so a bare Alt lands on the
  // chain's ROOT (AltPreviewFilter used to watch only the leaf and threw it away) — once
  // plainly, and once with the cursor resting on the toolbar button the flyout paints over,
  // where MainWindowEvents.cpp's qApp-wide Alt filter would pop ITS popover and steal the grab.
  void altHoldOverAnExportRowNeverClosesTheMenu() {
    // The offscreen QPA plugin doesn't honor Qt::ToolTip's real-platform contract of
    // coexisting with an open popup's grab, so exportPreview.cpp's preview tooltip closes
    // the menu there regardless of the fix (verified: reverting it reproduces the same
    // failure for real, and the quirk persists with the fix in place and with the tooltip's
    // own dust removed). Nothing to check here without a real windowing platform.
    if (QGuiApplication::platformName() == QLatin1String("offscreen"))
      QSKIP("Alt-hover's preview tooltip needs a real platform's popup-grab handling");
    enum Route { TOOLBAR_POPUP, NESTED_ROOT, NESTED_OVER_TOOLBAR_BUTTON };
    for (const Route route : {TOOLBAR_POPUP, NESTED_ROOT, NESTED_OVER_TOOLBAR_BUTTON}) {
      const char* name = route == TOOLBAR_POPUP ? "toolbar options popup"
                         : route == NESTED_ROOT ? "nested chain, Alt on the root"
                                               : "nested chain, cursor on the toolbar button";
      MainWindow win(nullptr, false);
      win.resize(1000, 760);
      win.show();
      QVERIFY2(QTest::qWaitForWindowExposed(&win), name);
      // Away from every icon before Alt is touched at all: QCursor::pos() is one
      // process-wide value outliving any window, and a stray pop_.buttons match opens a
      // modal that blocks forever in execMaybePopover's event loop (it hung the suite once).
      QCursor::setPos(win.mapToGlobal(QPoint(win.width() - 5, win.height() - 5)));
      win.openPathFromOS(guiTestImage());
      QTRY_VERIFY2(win.findChild<CanvasWidget*>()->hasImage(), name);
      QWidget* copyBtn = win.buttonForAction(win.actCopyImage_);
      QVERIFY2(copyBtn, name);

      if (route == TOOLBAR_POPUP) {
        // "Current"'s own row (actCopyImageCurrentRow_) only shows once something is
        // drawn — see currentRowHiddenWithNoLinesButToolbarButtonStays.
        stencil::core::Line line;
        line.points.push_back({4.0, 20.0});
        line.points.push_back({36.0, 20.0});
        win.canvas_->setLines({line});
        win.refreshActions();
        QContextMenuEvent ctx(QContextMenuEvent::Mouse, copyBtn->rect().center(),
                              copyBtn->mapToGlobal(copyBtn->rect().center()));
        QApplication::sendEvent(copyBtn, &ctx);
        QMenu* menu = win.copyImageOptionsMenu_;
        QVERIFY2(menu && menu->isVisible(), "the copy-image options popup never opened");
        // Hover the first row (QMenu::hovered is what wireExportPreviewHover listens on) so
        // AltPreviewFilter has an activeAction() to render a preview for. A synthetic
        // mouseMove doesn't reliably drive QMenu's own hover tracking on a real platform
        // popup, so set it directly — exactly what QMenu does internally on a real hover.
        menu->setActiveAction(win.actCopyImageCurrentRow_);
        QCOMPARE(menu->activeAction(), win.actCopyImageCurrentRow_);
        QTest::keyPress(menu, Qt::Key_Alt);
        QVERIFY2(menu->isVisible(), "holding Alt over an export row closed the menu");
        QTest::keyRelease(menu, Qt::Key_Alt);
        QVERIFY2(menu->isVisible(), "releasing Alt closed the menu");
        menu->close();
        // Let the preview's dust-out flight (DUST_OUT_MS, exportPreview.cpp) finish and its
        // DisintegrateOverlay — parented to this popup — be collected before `win` dies.
        QTest::qWait(260);
        continue;
      }

      const bool overButton = route == NESTED_OVER_TOOLBAR_BUTTON;
      bool reached = false, survived = false, previewShown = false, hijacked = false;
      QTimer::singleShot(0, [&] {
        QMenu* root = nullptr;
        for (int i = 0; i < 200 && !root; ++i) {
          root = qobject_cast<QMenu*>(QApplication::activePopupWidget());
          if (!root) QTest::qWait(10);
        }
        if (!root) return;
        QAction* layoutAct = nullptr;
        for (QAction* a : root->actions()) if (a->text() == "Image / Layout") layoutAct = a;
        if (!layoutAct || !layoutAct->menu()) { root->close(); return; }
        root->setActiveAction(layoutAct);
        QTest::keyClick(root, Qt::Key_Right);
        QMenu* layoutMenu = layoutAct->menu();
        settle([&] { return layoutMenu->isVisible(); }, 1000);
        QAction* copyAct = nullptr;
        for (QAction* a : layoutMenu->actions()) if (a->text().startsWith("Copy Image")) copyAct = a;
        if (!copyAct || !copyAct->menu()) { root->close(); return; }
        layoutMenu->setActiveAction(copyAct);
        QTest::keyClick(layoutMenu, Qt::Key_Right);
        QMenu* copyMenu = copyAct->menu();
        settle([&] { return copyMenu->isVisible(); }, 1000);
        if (!copyMenu->isVisible()) { root->close(); return; }
        // actCopyImageOriginal_, not actCopyImage_ itself: the latter is no longer a row in
        // this submenu at all (actCopyImageCurrentRow_ is — hidden with nothing drawn),
        // while Original is always there, and this route's point is Alt-key ROUTING rather
        // than which specific row it lands on.
        copyMenu->setActiveAction(win.actCopyImageOriginal_);
        reached = true;
        // underMouse() backs up the cursor-position check in MainWindowEvents.cpp (same
        // answer a real resting pointer leaves) and is what an offscreen-adjacent test can
        // mock — a real QCursor::setPos warp is not guaranteed to land in time.
        if (overButton) copyBtn->setAttribute(Qt::WA_UnderMouse, true);
        QTest::keyPress(root, Qt::Key_Alt);
        if (overButton) {
          QTest::qWait(30);
          hijacked = win.copyImageOptionsMenu_ && win.copyImageOptionsMenu_->isVisible();
        } else {
          // Checked directly, not just "did the menu survive" — a filter that does nothing
          // at all would trivially pass that half too.
          for (QWidget* w : QApplication::topLevelWidgets())
            if (w->objectName() == QLatin1String("exportPreviewTip") && w->isVisible())
              previewShown = true;
        }
        survived = copyMenu->isVisible() && layoutMenu->isVisible() && root->isVisible();
        QTest::keyRelease(root, Qt::Key_Alt);
        if (overButton) copyBtn->setAttribute(Qt::WA_UnderMouse, false);
        root->close();
        if (overButton && win.copyImageOptionsMenu_) win.copyImageOptionsMenu_->close();
      });
      win.showContextMenu(win.mapToGlobal(QPoint(500, 400)));
      QVERIFY2(reached, "never reached the nested Copy Image submenu");
      if (overButton)
        QVERIFY2(!hijacked, "Alt over the row opened the toolbar button's OWN options popup on top");
      else
        QVERIFY2(previewShown,
                 "Alt delivered to the chain's ROOT never reached the leaf's preview at all");
      QVERIFY2(survived, qPrintable(QString("%1: holding Alt closed the menu chain").arg(name)));
      QTest::qWait(260);
    }
  }

  // Alt+hover over the copy/download-image toolbar buttons themselves opens their
  // export-options popup, the SAME hold-to-peek gesture every other popover icon
  // gets (MainWindowEvents.cpp's pop_.peekExportMenu) — not just right-click/dblclick.
  // Releasing Alt closes it again unless the cursor moved inside it first (engaged).
  void altHoldOverExportButtonOpensItsOptionsPopup() {
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // QCursor::pos() is one process-wide value that outlives any one test/window —
    // a stray Alt keypress otherwise risks landing on WHATEVER popover button a
    // PRIOR test last left the (fake, offscreen) cursor sitting over, opening a
    // modal dialog that then blocks forever in execMaybePopover's QEventLoop::exec()
    // with nothing left to close it (regression: hung the whole suite, 300s
    // watchdog abort). Away from every icon before this test touches Alt at all.
    QCursor::setPos(win.mapToGlobal(QPoint(win.width() - 5, win.height() - 5)));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());

    QWidget* copyBtn = win.buttonForAction(win.actCopyImage_);
    QVERIFY(copyBtn);
    QMenu* menu = win.copyImageOptionsMenu_;
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

  // Under Fusion a chipped row's icon-to-label gap blows out unless the action's real
  // shortcut() is silenced for the life of the chip: with already-tabbed text AND a live
  // shortcut, QMenuPrivate reserves the shortcut column twice.
  void hotkeyChipDoesNotWidenTheIconGapUnderFusion() {
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());

    // actCopyImageOriginal_, not actCopyImage_: the latter is no longer a row in this
    // popup at all (actCopyImageCurrentRow_ is, and it carries no real shortcut of its
    // own by design — see MainWindow.hpp), but Original's Ctrl+Shift+C is exactly as
    // real and exactly as much MenuHotkeyChips' job to silence while chipped.
    const QKeySequence realShortcut = win.actCopyImageOriginal_->shortcut();
    QVERIFY2(!realShortcut.isEmpty(), "actCopyImageOriginal_ should carry a real shortcut to chip");

    QWidget* copyBtn = win.buttonForAction(win.actCopyImage_);
    QVERIFY(copyBtn);
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, copyBtn->rect().center(),
                          copyBtn->mapToGlobal(copyBtn->rect().center()));
    QApplication::sendEvent(copyBtn, &ctx);
    QMenu* menu = win.copyImageOptionsMenu_;
    const bool opened = menu && menu->isVisible();
    // Captured into locals and the menu closed BEFORE any assertion — an early
    // QVERIFY2 return must never leave the menu open, or it outlives `win` and
    // crashes on teardown (exportOptionsPopupIsNotWiderThanItsContent's own comment
    // has the full story — this test used to assert first, and the FALSE this
    // regression exposed took the whole process down with it, SIGSEGV, reported).
    bool silencedWhileChipped = false;
    QString cachedCombo;
    if (opened) {
      // While chipped: the native shortcut is silenced (that's the actual fix)...
      silencedWhileChipped = win.actCopyImageOriginal_->shortcut().isEmpty();
      // ...but the row still knows the real combo (property-cache, MenuHotkeys.hpp).
      cachedCombo = win.actCopyImageOriginal_->property("stencilHotkeyCombo").toString();
      menu->close();
      QTest::qWait(50);
    }
    QVERIFY2(opened, "the copy-image options popup never opened");
    QVERIFY2(silencedWhileChipped,
             "the action's native shortcut must be cleared while its row is chipped");
    QCOMPARE(cachedCombo, realShortcut.toString(QKeySequence::NativeText));
    QCOMPARE(win.actCopyImageOriginal_->shortcut(), realShortcut);   // restored once the chip is torn down
  }

  // A chipped row's keycaps shake once on hover (browser: .ctx-item:hover .tip-key /
  // keycapShake) — verified via capOffset(), "what the tests watch" per its own comment
  // (AppTooltip.hpp), and driven with setActiveAction() rather than QTest::mouseMove: the
  // latter does not reliably reach a shown popup's own hover tracking (confirmed — it left
  // QMenu::hovered's own spy at 0 — so it isn't a usable probe for this or any other
  // hover-driven popup behaviour), exactly the limitation contextMenuRowShimmersOnHover
  // already worked around for the sibling shimmer sweep. The guard around the shake
  // advances only on a genuinely NEW row: QMenu::hovered(QAction*) re-fires for the row
  // already hovered (setActiveAction re-emits it exactly as mouse jitter does) and a mouse
  // leaving a row without landing on another never fires it again, so it resets on
  // QEvent::Leave, like MenuShimmer.hpp's RowOverlay.
  void hotkeyChipShakeFollowsTheHoveredRow() {
    enum Case { SHAKES, REPLAYS_AFTER_LEAVE, NO_RESTART_ON_RE_FIRE };
    for (const Case which : {SHAKES, REPLAYS_AFTER_LEAVE, NO_RESTART_ON_RE_FIRE}) {
      const char* name = which == SHAKES ? "shakes on hover"
                         : which == REPLAYS_AFTER_LEAVE ? "replays after a leave and return"
                                                      : "no restart on a re-fire";
      MainWindow win(nullptr, false);
      win.resize(1000, 760);
      win.show();
      QVERIFY2(QTest::qWaitForWindowExposed(&win), name);
      win.openPathFromOS(guiTestImage());
      QTRY_VERIFY2(win.findChild<CanvasWidget*>()->hasImage(), name);
      // "Current"'s own row (actCopyImageCurrentRow_) only shows once something is
      // drawn — see currentRowHiddenWithNoLinesButToolbarButtonStays.
      {
        stencil::core::Line line;
        line.points.push_back({4.0, 20.0});
        line.points.push_back({36.0, 20.0});
        win.canvas_->setLines({line});
        win.refreshActions();
      }

      QWidget* copyBtn = win.buttonForAction(win.actCopyImage_);
      QVERIFY2(copyBtn, name);
      QContextMenuEvent ctx(QContextMenuEvent::Mouse, copyBtn->rect().center(),
                            copyBtn->mapToGlobal(copyBtn->rect().center()));
      QApplication::sendEvent(copyBtn, &ctx);
      QMenu* menu = win.copyImageOptionsMenu_;
      QVERIFY2(menu && menu->isVisible(), "the copy-image options popup never opened");

      QAction* row = win.actCopyImageCurrentRow_;
      QAction* other = nullptr;
      // Skip invisible rows too (actCopyImageSplit_ leads this same menu but stays hidden
      // outside compare mode) — setActiveAction on a row with no real geometry wouldn't
      // make the later move onto `row` a genuine transition.
      for (QAction* a : menu->actions())
        if (a != row && !a->isSeparator() && a->isVisible()) { other = a; break; }
      QVERIFY2(other, name);
      const QRect r = menu->actionGeometry(row);

      // NOT c->isHidden(): an action that's currently invisible (e.g. "Filter Only" with no
      // filter applied) still has its OWN chip widget parked wherever it was last valid —
      // geometry().intersects() alone can't tell a genuinely-showing chip from a hidden one
      // sitting in the same spot (MenuHotkeys.hpp's place() hides, never destroys them).
      stencil::gui::TipBody* chip = nullptr;
      for (QLabel* l : menu->findChildren<QLabel*>())
        if (auto* c = dynamic_cast<stencil::gui::TipBody*>(l))
          if (!c->isHidden() && c->geometry().intersects(r)) chip = c;
      QVERIFY2(chip, "no chip found for the Current row");
      // NOT chip->capCount() here — calling it is what LAZILY hunts the keycap regions, so
      // the test would prime the state MenuHotkeys.hpp's wire() must prime ITSELF. The rest
      // snapshot is taken first, grab()ing the chip exactly as wire() left it.
      const QImage rest = chip->grab().toImage();

      if (which == SHAKES) {
        bool sawNonZero = false;
        QImage midShake;
        // Land on a KNOWN different row first, so the move onto `row` is a genuine
        // transition (a freshly-opened QMenu can already be hovering its first row).
        menu->setActiveAction(other);
        menu->setActiveAction(row);
        for (int i = 0; i < 40 && !sawNonZero; ++i) {
          QTest::qWait(10);
          if (chip->capOffset() != 0) { sawNonZero = true; midShake = chip->grab().toImage(); }
        }
        menu->close();
        QVERIFY2(sawNonZero, "the chip's keycaps never moved during the shake window");
        QVERIFY2(!midShake.isNull() && midShake != rest,
                 "the shake changed capOffset() but never actually painted anything different "
                 "— the caps were never hunted, so paintEvent() had nothing to draw it with");
        continue;
      }

      if (which == REPLAYS_AFTER_LEAVE) {
        menu->setActiveAction(row);   // first hover: starts the shake
        QTRY_VERIFY2(chip->capOffset() != 0, "the shake should have started");
        // A plain wait long past the cycle's own length, not QTRY on ==0: the curve crosses
        // zero mid-cycle (see the re-fire case below), so QTRY would happily accept a
        // passing zero-crossing as "settled" while the shake is still running underneath.
        QTest::qWait(stencil::gui::AppTooltip::SHAKE_MS + 300);
        QCOMPARE(chip->capOffset(), 0);
        // The mouse leaves the row WITHOUT ever landing on another one — no second
        // hovered(QAction*) fires for that, only a real Leave.
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(menu, &leave);
        menu->setActiveAction(row);   // back onto the SAME row — must shake again
        QTRY_VERIFY2(chip->capOffset() != 0, "the shake should replay after the mouse came back");
        menu->close();
        continue;
      }

      // The shake curve crosses zero mid-cycle (it's a wiggle, not a one-way ramp), so a
      // single fixed-instant sample can land on a crossing and misread a live shake as
      // settled. QTRY catches it on the way up, and the re-fire and settle checkpoints are
      // timed off a real clock rather than guessed delays.
      QElapsedTimer timer;
      menu->setActiveAction(other);
      timer.start();
      menu->setActiveAction(row);   // first hover: starts the shake
      QTRY_VERIFY2(chip->capOffset() != 0, "the shake should have started");
      while (timer.elapsed() < 120) QTest::qWait(10);   // well clear of the start
      menu->setActiveAction(row);   // the re-fire — must NOT restart it
      // Wait to (a hair past) the ORIGINAL shake's own finish line, measured from when it
      // actually started. A wrongly-restarted shake would still be running here (its own
      // clock reset at the re-fire, well under SHAKE_MS old by this checkpoint); the
      // correctly-unbothered one has already settled back to rest.
      const int remaining = int(stencil::gui::AppTooltip::SHAKE_MS + 60 - timer.elapsed());
      if (remaining > 0) QTest::qWait(remaining);
      // Read the chip BEFORE closing: menu->close() tears down MenuHotkeyChips, which
      // deletes the chip widgets outright — reading through the pointer after that is a
      // use-after-free (previously the source of this test's own flakiness).
      const int settledOffset = chip->capOffset();
      menu->close();
      QCOMPARE(settledOffset, 0);
    }
  }

  // The variant popups take MenuHotkeyChips' `compact` mode — a tighter local stylesheet
  // plus a "\t"+spaces run sized to the chip's own width — rather than theme.cpp's generic
  // QMenu::item padding, which is sized for the menu bar's wider rows. This bounds the
  // SLACK only; per-chip fit is downloadPopupChipsAreNotClipped.
  void exportOptionsPopupIsNotWiderThanItsContent() {
    QApplication::setStyle(QStyleFactory::create("Fusion"));   // main.cpp forces this app-wide
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());

    QWidget* copyBtn = win.buttonForAction(win.actCopyImage_);
    QVERIFY(copyBtn);
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, copyBtn->rect().center(),
                          copyBtn->mapToGlobal(copyBtn->rect().center()));
    QApplication::sendEvent(copyBtn, &ctx);
    QMenu* menu = win.copyImageOptionsMenu_;
    QVERIFY2(menu && menu->isVisible(), "the copy-image options popup never opened");

    int widestLabel = 0;
    for (QAction* a : menu->actions()) {
      if (!a->isVisible()) continue;   // e.g. "Filter Only" with no filter applied
      const QString label = a->text().left(a->text().indexOf('\t'));
      widestLabel = std::max(widestLabel, menu->fontMetrics().horizontalAdvance(label));
    }
    int widestChip = 0;
    // Skip HIDDEN chips ("Filter Only" with no filter applied, "With Compare" outside
    // compare) — MenuHotkeys.hpp's place() hides rather than destroys them, so one can
    // still be sitting there with a nonzero width that never actually shows on screen.
    for (QLabel* l : menu->findChildren<QLabel*>())
      if (auto* chip = dynamic_cast<stencil::gui::TipBody*>(l))
        if (!chip->isHidden()) widestChip = std::max(widestChip, chip->width());
    QVERIFY2(widestChip > 0, "no hotkey chips found on the copy-image popup");

    // Icon + paddings + the gap between label and chip + the menu's own frame. A
    // generous ceiling (not an exact match) — it only has to catch the row coming out
    // FAR wider than its content, the actual regression.
    const int slack = menu->width() - (widestLabel + widestChip);
    // Closed BEFORE asserting, not after — an early QVERIFY2 return must never leave the
    // menu open, or it outlives `win` and crashes on teardown (downloadPopupChipsAreNotClipped's
    // own comment has the full story; this test used to assert first, so a failing slack
    // check here left the popup open and took the whole process down with it — SIGSEGV,
    // reported).
    menu->close();
    QVERIFY2(slack > 0 && slack <= 80,
             qPrintable(QString("menu is %1 wide for a %2px label + %3px chip — %4px of slack")
                            .arg(menu->width()).arg(widestLabel).arg(widestChip).arg(slack)));
  }

  // Per-chip "does it actually fit inside the menu", not just the aggregate slack the
  // case above bounds: setFixedWidth clips only the outer widget frame, never
  // QMenuPrivate's own sizeHint-driven row layout. The menu is closed BEFORE asserting —
  // a QMenu outliving `win` crashes on teardown.
  void downloadPopupChipsAreNotClipped() {
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());

    QWidget* saveBtn = win.buttonForAction(win.actSaveImage_);
    QVERIFY(saveBtn);
    QContextMenuEvent ctx(QContextMenuEvent::Mouse, saveBtn->rect().center(),
                          saveBtn->mapToGlobal(saveBtn->rect().center()));
    QApplication::sendEvent(saveBtn, &ctx);
    QMenu* menu = win.saveImageOptionsMenu_;
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

  // Clicking a CHECKABLE row in the canvas context menu must not recurse: StayOpenMenu
  // re-dispatches mouse events into the hosted chat panel and QApplication::notify walks
  // an unaccepted press back up into the menu. Checked with the assistant on AND off,
  // since only the enabled case has an interactive area at all.
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

    // The other half of the same hazard, and the one that actually recursed:
    // a click on a TRANSCRIPT ROW inside the chat panel. A QLabel ignores mouse
    // presses, so the re-dispatched event propagated back up to the menu.
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

      // Dragging the composer splitter goes through the SAME re-dispatch (plus
      // the move forwarding a drag needs) — it must not recurse either, and it
      // must actually resize.
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

  // The assistant submenu's gear opens the assistant-only settings dialog —
  // and closes the context menu FIRST. A modal dialog must never come up under
  // a menu that still holds the popup grab (it would be unfocused and behind
  // it), so the handler dismisses the menu chain and defers the dialog a turn.
  void contextMenuAssistantGearOpensSettings() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());   // the canvas menu opens for an image, and only then
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    win.settings_.llmProvider = "ollama";

    bool gearFound = false, menuGoneAfterClick = false, popupGrabGone = false;
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
      auto* gear = sub->findChild<QToolButton*>("chatMenuGear");
      gearFound = gear != nullptr;
      if (!gear) { menu->close(); return; }
      // Click it through the menu, the real popup path.
      QTest::mouseClick(sub, Qt::LeftButton, {},
                        gear->mapTo(sub, gear->rect().center()));
      menuGoneAfterClick = !menu->isVisible() && !sub->isVisible();
      popupGrabGone = QApplication::activePopupWidget() == nullptr;
    });
    win.showContextMenu(win.mapToGlobal(QPoint(400, 300)));
    QVERIFY2(gearFound, "the assistant submenu has no gear button");
    QVERIFY2(menuGoneAfterClick, "the gear left the context menu open");
    QVERIFY2(popupGrabGone, "the popup grab survived the gear click");

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
  // Menu-bar coverage: every browser toolbar section must be reachable from the menu bar
  // — Draw (start/stop plus the instant line and rect), the line-style set and the image
  // filter. Walking the real menu bar also proves the shared plain QActions were not
  // moved OUT of the context menu, which reusing a QWidgetAction would do.
  void menuBarExposesTheDrawAndStyleControls() {
    MainWindow win(nullptr, /*restoreLast=*/false);

    // Collect every action title reachable from the menu bar, submenus included.
    QSet<QString> titles;
    std::function<void(QMenu*)> walk = [&](QMenu* m) {
      for (QAction* a : m->actions()) {
        if (a->menu()) walk(a->menu());
        else if (!a->isSeparator()) titles.insert(a->text());
      }
    };
    for (QAction* top : win.menuBar()->actions())
      if (top->menu()) walk(top->menu());

    // The reported gap: instant line/rect, beside Start/Stop.
    QVERIFY(titles.contains("Start Drawing"));
    QVERIFY(titles.contains("Stop Drawing"));
    QVERIFY(titles.contains("Draw Line"));
    QVERIFY(titles.contains("Draw Rectangle"));

    // Line style (browser toolbar's Line Style select).
    QVERIFY(titles.contains("Solid"));
    QVERIFY(titles.contains("Dashed"));
    QVERIFY(titles.contains("Dotted"));

    // Image filter (browser toolbar's View section).
    QVERIFY(titles.contains("Cycle Image Filter"));
  }

  // The bug this locks down: a context-menu row whose action isn't available (no
  // image, no lines) used to show up greyed out with nothing to explain why — now it
  // is simply not in the menu, the desktop's version of the browser's hide-not-disable
  // (contextMenu.js syncState). The persistent menu bar keeps the conventional greyed
  // rows instead (menuBarExposesTheDrawAndStyleControls covers that one).
  void contextMenuHidesUnavailableActionsInsteadOfGreyingThem() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    // An image but NO lines — the menu needs an image to open at all, so the
    // line-dependent rows are what "unavailable" means here. Real right-click on the
    // scroll area's viewport (contextMenuOpensOnEmptyCanvasArea's own way in).
    win.openPathFromOS(guiTestImage());
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    QWidget* viewport = win.findChild<QScrollArea*>()->viewport();
    QVERIFY(viewport);

    auto openSubByKey = [](QMenu* menu, const QString& title) -> QMenu* {
      QAction* parent = nullptr;
      for (QAction* a : menu->actions())
        if (a->text().startsWith(title)) parent = a;
      if (!parent || !parent->menu()) return nullptr;
      menu->setActiveAction(parent);
      QTest::keyClick(menu, Qt::Key_Right);
      settle([&] { return !(!parent->menu()->isVisible()); }, 1000);
      return parent->menu()->isVisible() ? parent->menu() : nullptr;
    };

    QSet<QString> rootTitles, layoutTitles;
    QTimer::singleShot(0, [&] {
      QMenu* menu = nullptr;
      for (int i = 0; i < 200 && !menu; ++i) {
        menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) QTest::qWait(10);
      }
      if (!menu) return;
      for (QAction* a : menu->actions())
        if (!a->isSeparator()) rootTitles.insert(a->text());
      if (QMenu* layout = openSubByKey(menu, "Image / Layout"))
        for (QAction* a : layout->actions())
          if (!a->isSeparator()) layoutTitles.insert(a->text());
      menu->close();
    });
    QTest::mouseClick(viewport, Qt::RightButton, {}, QPoint(6, 6));
    QTest::qWait(50);

    // A row with a shortcut carries it appended as "\t<combo>" (native-rendered, so a
    // literal platform-specific suffix isn't reliable to match) — startsWith throughout,
    // exactly like openSubByKey above.
    auto has = [](const QSet<QString>& set, const QString& prefix) {
      for (const QString& t : set) if (t.startsWith(prefix)) return true;
      return false;
    };

    QVERIFY2(has(rootTitles, "Fit to Window"), "the context menu never opened");
    QVERIFY2(!has(rootTitles, "Clear All Lines"), "Clear All Lines showed with no lines to clear");

    QVERIFY2(!layoutTitles.isEmpty(), "the Image / Layout submenu never opened");
    // Line-dependent rows are the ones missing here — nothing is drawn yet.
    QVERIFY2(!has(layoutTitles, "Copy Layout JSON"), "Copy Layout showed with no lines to copy");
    QVERIFY2(!has(layoutTitles, "Export Layout JSON"), "Download Layout showed with no lines to download");
    // "Copy Image"/"Download Image" are the SUBMENU-OPENER titles (subMenuIn's own
    // arg) — a different, always-enabled QAction than actCopyImage_/actSaveImage_
    // itself, whose OWN text is the "Current (Tint + Lines/Points)" row nested
    // inside (contextMenuOpensOnEmptyCanvasArea's comment explains the same split).
    // They ride on the image, which this menu proves is there by existing at all.
    QVERIFY2(has(layoutTitles, "Copy Image"), "Copy Image hid with an image loaded");
    QVERIFY2(has(layoutTitles, "Download Image"), "Download Image hid with an image loaded");
    QVERIFY2(has(layoutTitles, "Paste Layout JSON"), "Paste Layout hid with an image loaded");
    // These two need neither an image nor lines — they always show.
    QVERIFY2(has(layoutTitles, "Paste (Image or Layout)"), "Paste Image needs no existing image");
    QVERIFY2(has(layoutTitles, "Import Layout JSON"), "Upload Layout needs no existing lines");
    beat();
  }

  // Browser parity: css/layout.css's ui-shimmer now covers .ctx-item too (support/
  // MenuShimmer.hpp is the desktop port) — the same left→right sweep every other
  // shimmered control gets (hoverShimmerAnimates), played on a context-menu ROW.
  void contextMenuRowShimmersOnHover() {
    const auto motion = withMotion();   // the sweep honours motionReduced(), which is on here
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());   // the canvas menu opens for an image, and only then
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    QWidget* viewport = win.findChild<QScrollArea*>()->viewport();
    QVERIFY(viewport);

    bool overlayFound = false, advanced = false;
    qreal p1 = -1.0, p2 = -1.0;
    QTimer::singleShot(0, [&] {
      QMenu* menu = nullptr;
      for (int i = 0; i < 200 && !menu; ++i) {
        menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) QTest::qWait(10);
      }
      if (!menu) return;
      QWidget* overlay = menu->findChild<QWidget*>("menuShimmerOverlay");
      if (!overlay) { menu->close(); return; }
      overlayFound = true;
      QCOMPARE(overlay->geometry(), menu->rect());  // the band covers the whole menu

      QAction* fit = nullptr;
      QAction* other = nullptr;
      for (QAction* a : menu->actions()) {
        if (a->isSeparator()) continue;
        if (a->text().startsWith("Fit to Window")) fit = a;
        else if (!other) other = a;
      }
      if (!fit || !other) { menu->close(); return; }
      // A freshly-opened QMenu can already be hovering its first row on its own —
      // land on a KNOWN different row first, so the move onto "fit" is a genuine
      // transition (sweep()'s own re-fire guard would no-op a same-row "hover").
      menu->setActiveAction(other);
      menu->setActiveAction(fit);
      p1 = overlay->property("sweepProgress").toReal();
      // Poll rather than a single timed sample: a QVariantAnimation ticks off
      // QMenu::exec()'s own event loop, which paces timers coarser than a normal
      // window's, so the SAME 325ms sweep can take a good deal longer, wall-clock,
      // to visibly move here than it does outside a popup (hoverShimmerAnimates).
      for (int i = 0; i < 400 && !advanced; ++i) {
        QTest::qWait(15);
        p2 = overlay->property("sweepProgress").toReal();
        advanced = p2 > p1 && p1 >= 0.0;
      }
      menu->close();
    });
    QTest::mouseClick(viewport, Qt::RightButton, {}, QPoint(6, 6));
    QTest::qWait(50);

    QVERIFY2(overlayFound, "no shimmer overlay on the context menu");
    QVERIFY2(advanced, qPrintable(QString("row shimmer did not advance (%1 -> %2)")
                                      .arg(p1)
                                      .arg(p2)));
    beat();
  }

  // MenuHotkeyChips shares ONE rows_ list across the whole recursive wire() tree while
  // each level's placer calls place() with THAT level's `menu`, so every row must be
  // checked against that menu — otherwise opening any submenu asks it for a root-level
  // row's geometry, gets an invalid rect, and hides the root menu's own chips.
  void openingASubmenuDoesNotHideTheRootMenusOwnChips() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());   // the canvas menu opens for an image, and only then
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    QWidget* viewport = win.findChild<QScrollArea*>()->viewport();
    QVERIFY(viewport);

    auto openSubByKey = [](QMenu* menu, const QString& title) -> QMenu* {
      QAction* parent = nullptr;
      for (QAction* a : menu->actions())
        if (a->text().startsWith(title)) parent = a;
      if (!parent || !parent->menu()) return nullptr;
      menu->setActiveAction(parent);
      QTest::keyClick(menu, Qt::Key_Right);
      settle([&] { return !(!parent->menu()->isVisible()); }, 1000);
      return parent->menu()->isVisible() ? parent->menu() : nullptr;
    };
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
  // The context menu's "Stencil Script" row is a FLYOUT, not an opener (browser
  // js/ui/ctxScript.js): a compact twin of the script window, hosted exactly like the
  // Assistant chat above it. Typing and running leave the menu open, the four actions
  // read Copy · Download · Upload · Run, and the typed script outlives the menu.
  void contextMenuScriptFlyout() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.openPathFromOS(guiTestImage());   // the canvas menu opens for an image, and only then
    QTRY_VERIFY(win.findChild<CanvasWidget*>()->hasImage());
    win.settings_.llmProvider = "ollama";   // browser: the row sits right under the Assistant
    auto* canvas = win.findChild<CanvasWidget*>();

    auto findMenu = []() -> QMenu* {
      QMenu* menu = nullptr;
      for (int i = 0; i < 200 && !menu; ++i) {
        menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) QTest::qWait(10);
      }
      return menu;
    };
    // startsWith, never == : the row carries its Alt+Shift+S hint in the "\t" column.
    auto scriptRow = [](QMenu* menu) -> QAction* {
      for (QAction* a : menu->actions())
        if (a->text().startsWith("Stencil Script")) return a;
      return nullptr;
    };

    bool isFlyout = false, keptHint = false, underAssistant = false, opened = false;
    bool fourActions = false, runIsPrimary = false, typedThrough = false, aliveAfterTyping = false;
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
      auto* run = sub->findChild<QPushButton*>("scriptMenuRun");
      if (!edit || !copy || !download || !upload || !run) { menu->close(); return; }
      fourActions = copy->x() < download->x() && download->x() < upload->x() &&
                    upload->x() < run->x();
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
    QVERIFY2(fourActions, "the actions are not Copy, Download, Upload, Run in that order");
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
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.menus.gui.moc"
