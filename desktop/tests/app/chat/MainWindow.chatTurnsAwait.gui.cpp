// MainWindow GUI e2e — an op plan across the I/O it waits on: each wait is a continuation, so the
// window stays live between an op and its answer, and a Stop or a Send there leaves the plan alone.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "connectionStore.hpp"
#include "../../support/heldImageServer.hpp"
#include "../../support/mockRest.hpp"

namespace {

  using stencil::test::HeldImageServer;

  stencil::llm::LlmReply planReply(const QString& actions) {
    stencil::llm::LlmReply reply;
    reply.ok = true;
    reply.text = QStringLiteral("{\"version\":1,\"reply\":\"On it.\",\"actions\":[%1]}").arg(actions);
    return reply;
  }

  // A drawn line, so the plan's shape never asks for a §7 continuation round.
  constexpr char DRAWN_LINE[] =
      "{\"op\":\"layout\",\"lines\":[{\"points\":[{\"x\":2,\"y\":2},{\"x\":15,\"y\":10}]}]}";

  bool isGray(const QImage& out) {
    const QRgb px = out.pixel(out.width() / 2, out.height() / 2);
    return qRed(px) == qGreen(px) && qGreen(px) == qBlue(px);
  }

}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

  // The echo rule: a plan may open only a URL the user wrote this conversation.
  static void userSays(MainWindow& win, const QString& text) {
    stencil::llm::ChatMessage m;
    m.role = QStringLiteral("user");
    m.text = text;
    win.chatSession->chatHistory.append(m);
  }

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // [connect, openUrl, filter, layout]: onChatReply returns with the plan parked at its first
  // await and no nested loop running; each answer resumes it, and every op lands in order.
  void planAcrossServerAndMediaWaitsRunsInOrder() {
    stencil::test::MockRest rest;
    QVERIFY(rest.listen());
    HeldImageServer http;
    QVERIFY(http.listen());
    http.hold = false;
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    stencil::net::connectionStore::saveServers({{rest.url(), QString(), QString()}});
    const auto forget = qScopeGuard([] { stencil::net::connectionStore::saveServers({}); });
    bool connectedAtFetch = false;
    http.onRequest = [&] {
      connectedAtFetch = win.remote.connections && win.remote.connections->urls().contains(rest.url());
    };
    userSays(win, QStringLiteral("connect, then open %1 and make it b&w").arg(http.url()));
    win.chatSession->onChatReply(planReply(
        QStringLiteral("{\"op\":\"connect\",\"server\":\"%1\"},{\"op\":\"openUrl\",\"url\":\"%2\"},"
                       "{\"op\":\"filter\",\"mode\":\"bw\"},{\"op\":\"layout\",\"lines\":[{\"points\":"
                       "[{\"x\":2,\"y\":2},{\"x\":15,\"y\":10}]}]}")
            .arg(rest.url(), http.url())));
    QVERIFY2(win.chatSession->planRunning, "the plan is parked at its first await");
    QVERIFY2(win.pop.loops.isEmpty(), "and no nested event loop holds it");
    QCOMPARE(win.pop.awaits.size(), qsizetype(1));
    QTRY_VERIFY_WITH_TIMEOUT(!win.chatSession->planRunning, 10000);
    QVERIFY(win.pop.awaits.isEmpty());
    QVERIFY2(connectedAtFetch, "the picture was fetched only once the connection was up");
    QCOMPARE(win.canvas->effectiveOriginalImage().size(), QSize(20, 14));
    QVERIFY2(isGray(win.canvas->renderToImage(false)), "the filter landed on the fetched picture");
    QCOMPARE(int(win.canvas->allLines().size()), 1);
    QVERIFY(assistantBubbleTexts(win.chatDock).contains(QStringLiteral("On it.")));
    QVERIFY(!chatTranscriptHas(win.chatDock, QStringLiteral("timed out")));
    beat();
  }

  // Mid-plan the dock is idle, as it always was: a Stop has no turn to stop, so the plan runs on.
  void stopMidPlanLeavesThePlanRunning() {
    HeldImageServer http;
    QVERIFY(http.listen());
    MainWindow win(nullptr, false);
    win.resize(1000, 760);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    userSays(win, QStringLiteral("open %1 in black and white").arg(http.url()));
    win.chatSession->onChatReply(planReply(
        QStringLiteral("{\"op\":\"openUrl\",\"url\":\"%1\"},{\"op\":\"filter\",\"mode\":\"bw\"},%2")
            .arg(http.url(), QString::fromLatin1(DRAWN_LINE))));
    QTRY_VERIFY(!http.waiting.isEmpty());
    QVERIFY(win.chatSession->planRunning);
    QVERIFY(!win.chatDock->isBusy());
    emit win.chatDock->stopRequested();
    win.chatSession->onChatStop();
    QVERIFY(!win.chatSession->chatStopRequested);
    QVERIFY(win.chatSession->planRunning);
    http.release();
    QTRY_VERIFY_WITH_TIMEOUT(!win.chatSession->planRunning, 10000);
    QVERIFY2(isGray(win.canvas->renderToImage(false)), "the plan ran to its end");
    QVERIFY(!chatTranscriptHas(win.chatDock, QStringLiteral("Stopped.")));
    QVERIFY(!win.chatSession->chatStopRequested);
    beat();
  }

  // A Send while the plan holds the canvas is refused, as before — but its text stays in the
  // composer it was typed in, the dock's and the menu panel's alike, instead of being lost.
  void sendMidPlanKeepsTheComposerText() {
    HeldImageServer http;
    QVERIFY(http.listen());
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(chat);
    chat->setChecked(true);
    QTRY_VERIFY(win.chatDock->isVisible());
    auto* input = win.chatDock->findChild<QPlainTextEdit*>("chatInput");
    auto* send = win.chatDock->findChild<QToolButton*>("chatSend");
    QVERIFY(input && send);
    userSays(win, QStringLiteral("open %1").arg(http.url()));
    win.chatSession->onChatReply(planReply(
        QStringLiteral("{\"op\":\"openUrl\",\"url\":\"%1\"},{\"op\":\"filter\",\"mode\":\"bw\"},%2")
            .arg(http.url(), QString::fromLatin1(DRAWN_LINE))));
    QTRY_VERIFY(!http.waiting.isEmpty());
    const int histBefore = win.chatSession->chatHistory.size();

    input->setPlainText(QStringLiteral("now crop it"));
    QTest::mouseClick(send, Qt::LeftButton);
    QCOMPARE(input->toPlainText(), QStringLiteral("now crop it"));
    QTest::keyClick(input, Qt::Key_Return);
    QCOMPARE(input->toPlainText(), QStringLiteral("now crop it"));
    win.chatSession->ensureChatMenuPanel();
    auto* menuInput = qobject_cast<QPlainTextEdit*>(win.chatSession->chatMenuInput);
    QVERIFY(menuInput);
    menuInput->setPlainText(QStringLiteral("and rotate"));
    QTest::keyClick(menuInput, Qt::Key_Return);
    QCOMPARE(menuInput->toPlainText(), QStringLiteral("and rotate"));
    QCOMPARE(win.chatSession->chatHistory.size(), histBefore);
    QVERIFY(!win.chatDock->isBusy());

    http.release();
    QTRY_VERIFY_WITH_TIMEOUT(!win.chatSession->planRunning, 10000);
    QCOMPARE(input->toPlainText(), QStringLiteral("now crop it"));
    QCOMPARE(win.chatSession->chatHistory.size(), histBefore);
    chat->setChecked(false);
    QTRY_VERIFY(!win.chatDock->isVisible());
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatTurnsAwait.gui.moc"
