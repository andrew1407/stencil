// MainWindow GUI e2e — openUrl awaiting its load, and the continuations a layout op does or does not get.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // §10 openUrl awaits the load + amended §7: a plan [openUrl, filter] lands the filter on the
  // fetched picture (no "no working image" race with the async MediaLoader), then continues ONCE.
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

  // §7 one-reply companion: a load-shaped plan HOLDS its bubble, but when the continuation cannot
  // launch (here a text-only model) the held reply posts right then — one bubble, no round 2.
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

  // §7: a ZERO-line layout op validates but draws nothing, so a [blank, layout{lines:[]}] plan must
  // still continue — counting it as "drew" suppressed the very round meant to draw.
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

  // §7 companion: a layout op with REAL lines committed to its coordinates must NOT continue, and
  // (§3.0) must not send anything else either: one round, done.
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

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatTurnsContinue.gui.moc"
