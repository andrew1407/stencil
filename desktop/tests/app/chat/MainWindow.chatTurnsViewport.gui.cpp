// MainWindow GUI e2e — A row the transcript is CLIPPING still has a reachable "…".
// The placement checks it runs on each surface are in chatViewportGui.hpp.
#include "chatViewportGui.hpp"

using namespace stencil::guitest;

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // A row the transcript is CLIPPING still has a reachable "…": parked at the intersection of card
  // and viewport, not the card's own bottom. Clipped both ways, docked, floating and in the panel.
  void chatRowMenuStaysInsideTheViewport() {
    MainWindow win(nullptr, false);
    win.resize(1100, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings.llmProvider = "ollama";
    win.settings.llmBaseUrl = "http://localhost:11434";
    win.acts.chat->setChecked(true);
    QTRY_VERIFY(win.chatDock->isVisible());
    // Enough long messages that the transcript really scrolls: bubbles stretch to the full cap width
    // once they wrap (applyChatBubbleWidths), so more turns are needed to leave a row clipped.
    for (int i = 0; i < 14; ++i) {
      win.chatDock->appendUser(
          QStringLiteral("Loading the image into incognito, converting to black & white "
                         "and cropping to portrait 3:4. Once it's done I will report "
                         "back with the result (%1).").arg(i));
      win.chatDock->appendAssistant(
          QStringLiteral("Working on it — this reply is deliberately long so the row is "
                         "taller than a line and gets clipped by the viewport edge "
                         "while scrolling (%1).").arg(i));
    }
    settleLayout(win.chatDock, 300);
    const auto pillsBox = [&] {
      return static_cast<stencil::gui::ChatDock*>(win.chatDock)->jumpPillsGlobalRect();
    };

    QScrollArea* dockScroll = nullptr;
    for (QScrollArea* a : win.chatDock->findChildren<QScrollArea*>()) dockScroll = a;
    checkSurface(win.chatDock, dockScroll, "docked");
    checkSliver(pillsBox, win.chatDock, dockScroll, "docked");
    win.chatDock->setMinimumWidth(0);
    win.resizeDocks({win.chatDock}, {230}, Qt::Horizontal);   // squeeze it
    settleLayout(win.chatDock, 300);
    checkNarrow(pillsBox, win.chatDock, dockScroll, "docked narrow");

    // The floating/compact shape uses the same transcript widget.
    win.chatDock->setFloating(true);
    win.chatDock->resize(360, 460);
    settleLayout(win.chatDock, 300);
    checkSurface(win.chatDock, dockScroll, "floating");
    win.chatDock->resize(240, 460);   // narrow float: bubbles at both edges
    settleLayout(win.chatDock, 300);
    checkNarrow(pillsBox, win.chatDock, dockScroll, "floating narrow");
    win.chatDock->setFloating(false);
    settleLayout(win.chatDock, 200);

    // …and so does the context menu's panel.
    win.chatSession->ensureChatMenuPanel();
    QVERIFY(win.chatSession->chatMenuPanel);
    // It normally lives inside the menu's QWidgetAction; show it in place so it
    // lays out (a hidden scroll area has no range to scroll).
    win.chatSession->chatMenuPanel->setGeometry(20, 20, 340, 640);
    win.chatSession->chatMenuPanel->show();
    // The panel is built lazily and mirrors the SHARED history, which these
    // direct dock appends never touched — mirror the same volume into it.
    for (int i = 0; i < 14; ++i) {
      win.chatSession->chatMirror(QStringLiteral("You"),
                     QStringLiteral("Loading the image into incognito, converting to "
                                    "black & white and cropping to portrait 3:4 (%1).").arg(i),
                     false);
      win.chatSession->chatMirror(QStringLiteral("Assistant"),
                     QStringLiteral("Working on it — this reply is deliberately long so "
                                    "the row is taller than a line and gets clipped by "
                                    "the viewport edge while scrolling (%1).").arg(i),
                     false);
    }
    settleLayout(win.chatSession->chatMenuPanel, 200);
    checkSurface(win.chatSession->chatMenuPanel,
                 win.chatSession->chatMenuPanel->findChild<QScrollArea*>("chatMenuTranscript"), "menu panel");
    beat();
  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatTurnsViewport.gui.moc"
