// MainWindow GUI e2e — Sliding's docked-chat pour holds the chat's body still under its cover: no re-wrap and
// no paint at each width of the slide, and every constraint handed back when the veil drops
// (app/chrome/DockChromePour.cpp freezeChatBody). Shared ground is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

#include <QDockWidget>

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // Frozen, the body keeps its width while the dock's pinned extent slides under it; thawed, it follows the dock again.
  void chatBodyHoldsItsWidthUnderThePour() {
    MainWindow win(nullptr, false);
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.chatDock->show();
    settleLayout(&win, 120);
    QWidget* body = win.chatDock->widget();
    QVERIFY(body);
    const int full = body->width();
    QVERIFY(full > 120);
    auto& chrome = win.parts.dockChrome;
    chrome.freezeChatBody(/*horiz=*/true);
    QVERIFY(!body->updatesEnabled());
    if (QWidget* bar = win.chatDock->titleBarWidget()) QVERIFY(!bar->updatesEnabled());
    chrome.chatExtentPin(true)(full / 2);
    settleLayout(&win, 60);
    QCOMPARE(body->width(), full);
    chrome.dropChatVeil();
    QVERIFY(body->updatesEnabled());
    if (QWidget* bar = win.chatDock->titleBarWidget()) QVERIFY(bar->updatesEnabled());
    QCOMPARE(body->minimumWidth(), 0);
    QCOMPARE(body->maximumWidth(), QWIDGETSIZE_MAX);
    settleLayout(&win, 60);
    QVERIFY(body->width() < full);
    chrome.stopChatAnim();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatPourFreeze.gui.moc"
