// MainWindow GUI e2e — The Assistant flyout's composer: the dock's own button pair, chips and transcript.
// Shared ground is in menusAssistant.gui.hpp, over MainWindow.gui.hpp.
#include "menusAssistant.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The menu composer is the DOCK's, not a second design: the same send + “…” pair, the same
  // suggestion chips prefilling rather than sending, and a transcript tall enough to read.
  void assistantComposerMatchesTheDock() {
    MainWindow win(nullptr, false);
    QVERIFY(showWithWorkingImage(win));
    MockChatTransport mock;
    armSepiaAssistant(win.settings, win.parts.chatAppliers.llmClient, mock);

    bool twoButtons = false, dotOnMore = false, buttonsMatchDock = false;
    bool hintGone = false, sendGatedEmpty = false;
    bool chipsShownEmpty = false, chipTextsMatchDock = false, chipPrefilled = false;
    bool chipDidNotSend = false;
    QStringList overflowItems;
    int chipCount = 0, transcriptCap = 0;
    bool assistantOpened = false;
    QTimer::singleShot(0, [&] {
      const AssistantFlyout f = openAssistantFlyout();
      assistantOpened = f.sub != nullptr;
      if (!f.complete()) return;
      QMenu* menu = f.menu;
      QMenu* sub = f.sub;
      QWidget* panel = f.panel;
      QPlainTextEdit* input = f.input;
      QToolButton* sendBtn = f.send;
      QToolButton* moreBtn = f.more;
      // The browser's TWO composer buttons, in order: send · "…", with attach and
      // settings folded into the overflow the reachability dot now rides on.
      twoButtons = sendBtn->x() < moreBtn->x() &&
                   !sub->findChild<QToolButton*>("chatMenuAttach") &&
                   !sub->findChild<QToolButton*>("chatMenuGear");
      if (auto* over = moreBtn->findChild<QMenu*>("chatMenuMoreMenu"))
        for (QAction* a : over->actions()) overflowItems << a->text();
      // …and the DOCK's exact look: same 18 px glyphs, same 26 px box, same
      // accent treatment, so the two composers read identically.
      auto* dockSend = win.chatDock->findChild<QToolButton*>("chatSend");
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
            win.chatDock->findChildren<QPushButton*>("chatSuggestChip");
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
      auto* dot = sub->findChild<QLabel*>("chatMenuStatusDot");
      dotOnMore = dot && dot->parentWidget() == moreBtn;
      // Room for a reply plus a couple of exchanges without scrolling — the APPLIED height, not the
      // constant: a plain maximumHeight let the scroll area collapse to its ~2-row content sizeHint.
      if (auto* scrollArea = panel->findChild<QScrollArea*>("chatMenuTranscript"))
        transcriptCap = scrollArea->height();
      QCOMPARE(input->placeholderText(),
               QString("Ask the assistant… (Enter sends, Shift+Enter newline)"));
      // It must not take focus implicitly — menu navigation stays live until
      // the user actually clicks into the input.
      QCOMPARE(input->focusPolicy(), Qt::ClickFocus);
      menu->close();
    });
    win.parts.canvasMenu.showContextMenu(win.mapToGlobal(QPoint(400, 300)));

    QVERIFY2(assistantOpened, "the Assistant submenu did not open");
    QVERIFY2(buttonsMatchDock, "the menu composer buttons do not match the dock's");
    QVERIFY2(hintGone, "the explanatory hint line is still in the submenu");
    QCOMPARE(chipCount, 4);
    QVERIFY2(chipsShownEmpty, "no suggestion chips in the menu's empty state");
    QVERIFY2(chipTextsMatchDock, "the menu chips differ from the dock's");
    QVERIFY2(chipPrefilled, "clicking a chip did not prefill the composer");
    QVERIFY2(chipDidNotSend, "clicking a chip sent instead of prefilling");
    QVERIFY2(sendGatedEmpty,
             "the composer pair is not one set (size/box/accent) with send merely disabled");
    QVERIFY2(twoButtons, "the menu composer is not the browser's send + \"…\" pair");
    QCOMPARE(overflowItems, QStringList({"Add image", "Swap message sides", "Settings"}));
    QVERIFY2(dotOnMore, "the provider status dot is not on the menu's \"…\"");
    QVERIFY2(transcriptCap >= 140,
             qPrintable(QString("the menu transcript renders only %1 px tall")
                            .arg(transcriptCap)));

    win.parts.chatAppliers.llmClient.reset();  // drop the mock before it goes out of scope
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.menusComposer.gui.moc"
