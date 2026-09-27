// The assistant shots: the real model when the state dir carries a server (see main), else a
// canned plan through the real client — either way the canvas really changes.
#include "captureShared.hpp"

#include "CanvasWidget.hpp"
#include "ChatDock.hpp"
#include "LlmClient.hpp"
#include <QLabel>
#include <QPlainTextEdit>
#include <QtTest/QTest>

using namespace stencil::gui;

namespace {
  const QString ICON_URL = envOr("STENCIL_DOCS_ICON_URL",
      "https://raw.githubusercontent.com/andrew1407/stencil/main/bot/assets/icon.png");
  const QString PROMPT = envOr("STENCIL_DOCS_PROMPT",
      "Warm it up with sepia, outline the centre and show me two more takes");
  const QString REAL_PROMPT = envOr("STENCIL_DOCS_REAL_PROMPT",
      "Frame the S with a dashed red rectangle and warm the picture up with sepia");

  const QString PLAN = envOr("STENCIL_DOCS_PLAN", R"({"version":1,"reply":"Applied a sepia tint and framed the centre with a dashed outline. Two variants: one rotated a quarter turn, one in black and white.","actions":[{"op":"filter","mode":"sepia"},{"op":"layout","lines":[{"points":[{"x":96,"y":64},{"x":864,"y":64},{"x":864,"y":576},{"x":96,"y":576},{"x":96,"y":64}],"color":"#1e63c8","thickness":3,"pointSize":4,"style":"dashed","locked":false,"fillColor":"transparent"}]}],"variants":[{"label":"rotated","actions":[{"op":"rotate","dir":"right"}]},{"label":"black & white","actions":[{"op":"filter","mode":"bw"}]}]})");

  // Canned ollama-shaped reply, the seam LlmClient.headless.cpp and the chat GUI suites use.
  struct CannedTransport : stencil::llm::LlmTransport {
    void postJson(const QUrl&, const QList<QPair<QByteArray, QByteArray>>&, const QByteArray&,
                  std::function<void(int, QByteArray, QString)> cb) override {
      const QJsonObject wire{{"message", QJsonObject{{"content", PLAN}}}};
      cb(200, QJsonDocument(wire).toJson(QJsonDocument::Compact), QString());
    }
    void getJson(const QUrl&, const QList<QPair<QByteArray, QByteArray>>&,
                 std::function<void(int, QByteArray, QString)> cb) override {
      cb(200, QByteArrayLiteral("{}"), QString());
    }
  };

  // The turn is over when a second card body (the reply) exists and no card is the pending "…".
  bool replied(QWidget* dock) {
    int bodies = 0;
    bool pending = false;
    for (QLabel* l : dock->findChildren<QLabel*>()) {
      const QString b = l->property("chatBody").toString();
      if (b.isEmpty()) continue;
      if (b == QStringLiteral("…")) pending = true; else ++bodies;
    }
    return bodies >= 2 && !pending;
  }
}  // namespace

void MainWindowGuiTest::assistantShots(MainWindow& win, const QString& theme, const ShotSet& shots) {
  static CannedTransport canned;
  auto* canvas = win.findChild<CanvasWidget*>();
  // The assistant: the real model when the state dir carries a server (see main), else a
  // canned plan through the real client — either way the canvas really changes.
  const QString assistantDocked = suffixed("assistant-docked", theme);
  if (shots.hasAny({assistantDocked, QStringLiteral("assistant-floating")})) {
    const bool real = win.settings.llmProvider == QLatin1String("stencil-server");
    if (real) {
      win.parts.sourceOpener.openSourceHere(ICON_URL, 0, false);
      waitUntil([canvas] { return canvas->hasImage() && canvas->getImage().width() == 512; }, 15000);
      fit(win);
    } else {
      win.settings.llmProvider = "ollama";
      win.parts.chatAppliers.llmClient = std::make_unique<stencil::llm::LlmClient>(&canned);
    }
    ChatDock* chat = win.chatDock;
    win.addDockWidget(Qt::RightDockWidgetArea, chat, Qt::Horizontal);
    chat->setFloating(false);
    win.acts.chat->setChecked(true);
    waitUntil([chat] { return chat->isVisible(); });
    auto* input = chat->findChild<QPlainTextEdit*>("chatInput");
    if (input) {
      input->setPlainText(real ? REAL_PROMPT : PROMPT);
      QTest::keyClick(input, Qt::Key_Return);
      if (!waitUntil([chat] { return replied(chat); }, real ? 150000 : 10000)) {
        std::printf("  assistant: no reply —");
        for (QLabel* l : chat->findChildren<QLabel*>())
          if (!l->property("chatNote").toString().isEmpty()) std::printf(" [%s]", qPrintable(l->property("chatNote").toString()));
        std::printf("\n");
      }
      pumpFor(400);
      fit(win);
      if (shots.has(assistantDocked)) save(assistantDocked, &win);
      if (shots.has("assistant-floating")) {
        chat->setFloating(true);
        chat->resize(420, 560);
        pumpFor(200);
        save("assistant-floating", &win);
        chat->setFloating(false);
      }
    }
    win.acts.chat->setChecked(false);
    pumpFor(100);
  }
}
