// MainWindow GUI e2e — a chat turn on the anthropic wire: with no session key nothing is sent and the
// card asks for the key; with one, the window's own transport posts to a loopback /v1/messages.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"
#include "SessionKey.hpp"
#include <optional>

namespace {
  const QString KEY = QStringLiteral("sk-ant-e2e-0123456789abcdef");

  // One HTTP/1.1 exchange the loopback provider saw: the request line, lower-cased headers, body.
  struct Seen {
    QByteArray line;
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;
  };

  // Parses whole requests off a socket; `answer` gives each its JSON reply (connection closed after).
  void serveJson(QTcpServer& http, QList<Seen>& seen, std::function<QByteArray(const Seen&)> answer) {
    QObject::connect(&http, &QTcpServer::newConnection, &http, [&http, &seen, answer] {
      QTcpSocket* s = http.nextPendingConnection();
      auto buf = std::make_shared<QByteArray>();
      QObject::connect(s, &QTcpSocket::readyRead, s, [s, buf, &seen, answer] {
        *buf += s->readAll();
        const int end = buf->indexOf("\r\n\r\n");
        if (end < 0) return;
        Seen r;
        const QList<QByteArray> lines = buf->left(end).split('\n');
        r.line = lines.first().trimmed();
        for (int i = 1; i < lines.size(); ++i) {
          const int colon = lines[i].indexOf(':');
          if (colon > 0) r.headers.insert(lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed());
        }
        const int length = r.headers.value("content-length", "0").toInt();
        if (buf->size() < end + 4 + length) return;
        r.body = buf->mid(end + 4, length);
        seen.append(r);
        const QByteArray json = answer(r);
        s->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                 QByteArray::number(json.size()) + "\r\nConnection: close\r\n\r\n" + json);
        s->disconnectFromHost();
      });
      QObject::connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
    });
  }
}  // namespace

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }
  void init() { stencil::llm::SessionKey::instance().forget(); }

  // No key held: the turn sends nothing, the card says so and its Configure button opens the
  // assistant settings on the key field.
  void noKeyTurnAsksForTheKey() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings.llmProvider = QStringLiteral("anthropic");
    win.settings.llmBaseUrl = QStringLiteral("http://127.0.0.1:1");
    MockChatTransport mock;
    win.parts.chatAppliers.llmClient = std::make_unique<stencil::llm::LlmClient>(&mock);
    win.findChild<QAction*>("actChat")->setChecked(true);
    openTranscript(win);

    win.chatSession->onChatSend(QStringLiteral("crop 10% off the left"));
    QTRY_VERIFY(!win.chatDock->isBusy());
    QVERIFY2(mock.allBodies.isEmpty(), "a turn with no session key must send nothing");
    QVERIFY(chatTranscriptHas(win.chatDock, QStringLiteral(
        "Anthropic API (Claude): no API key for this session — enter your key in the assistant settings.")));
    auto* cta = win.chatDock->findChild<QPushButton*>("chatConfigureCta");
    QVERIFY(cta);
    QString dialogName;
    bool onKeyField = false;
    QTimer::singleShot(0, [&] {
      QDialog* dlg = nullptr;
      for (int i = 0; i < 200 && !dlg; ++i) {
        dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dlg) QTest::qWait(10);
      }
      if (!dlg) return;
      dialogName = dlg->objectName();
      onKeyField = dlg->focusWidget() == dlg->findChild<QLineEdit*>("llmAnthropicKey");
      dlg->reject();
    });
    cta->click();
    QCOMPARE(dialogName, QString("assistantSettingsDialog"));
    QVERIFY2(onKeyField, "Configure provider should open on the key entry");
    win.parts.chatAppliers.llmClient.reset();
    beat();
  }

  // A held key refused over plain http to a LAN host: nothing is sent, and the card names the
  // fix (https) instead of asking for the key it already has.
  void plainHttpRefusalNamesTheFix() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings.llmProvider = QStringLiteral("anthropic");
    win.settings.llmBaseUrl = QStringLiteral("http://lan.test");
    MockChatTransport mock;
    win.parts.chatAppliers.llmClient = std::make_unique<stencil::llm::LlmClient>(&mock);
    stencil::llm::SessionKey::instance().hold(KEY);
    win.findChild<QAction*>("actChat")->setChecked(true);
    openTranscript(win);

    win.chatSession->onChatSend(QStringLiteral("crop 10% off the left"));
    QTRY_VERIFY(!win.chatDock->isBusy());
    QVERIFY2(mock.allBodies.isEmpty(), "a refused plain-http turn must send nothing");
    QVERIFY(chatTranscriptHas(win.chatDock, QStringLiteral("Anthropic API (Claude): refusing to send the API key "
                                                           "to 'lan.test' over plain http — use https.")));
    win.parts.chatAppliers.llmClient.reset();
    beat();
  }

  // With a key held, the real QtLlmTransport posts the §6.5 request to the configured base URL — a
  // loopback server here — and the reply lands; the key is on no disk afterwards.
  void loopbackTurnCarriesTheKey() {
    QTcpServer http;
    QVERIFY(http.listen(QHostAddress::LocalHost, 0));
    QList<Seen> seen;
    serveJson(http, seen, [](const Seen& r) -> QByteArray {
      const QJsonObject model{{"type", "model"}, {"id", "claude-opus-5"}};
      const QString plan = QStringLiteral("{\"version\":1,\"reply\":\"Hello from the loopback.\",\"actions\":[]}");
      const QJsonObject reply{{"model", "claude-opus-5"}, {"stop_reason", "end_turn"},
                              {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", plan}}}}};
      return QJsonDocument(r.line.startsWith("GET") ? QJsonObject{{"data", QJsonArray{model}}} : reply)
          .toJson(QJsonDocument::Compact);
    });

    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    win.settings.llmProvider = QStringLiteral("anthropic");
    win.settings.llmBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(http.serverPort());
    win.settings.llmModel.clear();
    win.parts.chatAppliers.llmClient.reset();   // the window's own transport, not a mock
    stencil::llm::SessionKey::instance().hold(KEY);
    win.findChild<QAction*>("actChat")->setChecked(true);
    openTranscript(win);

    win.chatSession->onChatSend(QStringLiteral("hello"));
    QTRY_VERIFY_WITH_TIMEOUT(!win.chatDock->isBusy(), 8000);
    std::optional<Seen> post;
    for (const Seen& r : seen)
      if (r.line.startsWith("POST")) post = r;
    QVERIFY2(post, "no POST reached the loopback provider");
    QCOMPARE(post->line, QByteArray("POST /v1/messages HTTP/1.1"));
    QCOMPARE(post->headers.value("x-api-key"), KEY.toUtf8());
    QCOMPARE(post->headers.value("anthropic-version"), QByteArray("2023-06-01"));
    QVERIFY(post->headers.value("content-type").startsWith("application/json"));
    QVERIFY2(!post->headers.contains("authorization"), "no Authorization on the anthropic wire");
    QVERIFY2(!post->headers.contains("anthropic-dangerous-direct-browser-access"),
             "a desktop app is not a browser page");
    const QJsonObject body = QJsonDocument::fromJson(post->body).object();
    QCOMPARE(body.value("model").toString(), QString("claude-opus-5"));
    QCOMPARE(body.value("max_tokens").toInt(), 32768);
    QVERIFY(!body.value("system").toString().isEmpty());
    const QJsonArray content = body.value("messages").toArray().last().toObject().value("content").toArray();
    QCOMPARE(content.first().toObject().value("text").toString(), QString("hello"));
    QVERIFY(assistantBubbleTexts(win.chatDock).join('\n').contains("Hello from the loopback."));

    win.applySettings(win.settings, true);   // the window's own save path
    const QStringList leaks = placesHolding(KEY);
    QVERIFY2(leaks.isEmpty(), qPrintable(leaks.join(", ")));
    stencil::llm::SessionKey::instance().forget();
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatAnthropic.gui.moc"
