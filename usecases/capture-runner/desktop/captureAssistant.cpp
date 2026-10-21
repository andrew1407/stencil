// The assistant shots over Stencil's icon: the real model behind a server's proxy when the run
// asks for one (see main), else the canned plan through the real client — either way the canvas
// really changes. Docked on the left, then floating over the window.
#include "captureShared.hpp"

#include "CanvasWidget.hpp"
#include "ChatDock.hpp"
#include "LlmClient.hpp"
#include <QLabel>
#include <QPlainTextEdit>
#include <QTimer>
#include <QtTest/QTest>

using namespace stencil::gui;

namespace {
  // The picture the plan is written for (512x512; bot/assets/icon.png), opened whole.
  const QString ICON_FILE = envOr("STENCIL_DOCS_ICON_FILE", "");
  const QString PROMPT = envOr("STENCIL_DOCS_PROMPT",
      "Frame the S with a dashed red rectangle, warm the picture up with sepia and show me two more takes");
  const QString REAL_PROMPT = envOr("STENCIL_DOCS_REAL_PROMPT",
      "Frame the S with a dashed red rectangle and warm the picture up with sepia");

  const QString PLAN = envOr("STENCIL_DOCS_PLAN", R"({"version":1,"reply":"Applied a sepia tint and framed the centre with a dashed outline. Two variants: one rotated a quarter turn, one in black and white.","actions":[{"op":"filter","mode":"sepia"},{"op":"layout","lines":[{"points":[{"x":96,"y":64},{"x":864,"y":64},{"x":864,"y":576},{"x":96,"y":576},{"x":96,"y":64}],"color":"#1e63c8","thickness":3,"pointSize":4,"style":"dashed","locked":false,"fillColor":"transparent"}]}],"variants":[{"label":"rotated","actions":[{"op":"rotate","dir":"right"}]},{"label":"black & white","actions":[{"op":"filter","mode":"bw"}]}]})");

  // Canned ollama-shaped reply, the seam LlmClient.headless.cpp and the chat GUI suites use.
  struct CannedTransport : stencil::llm::LlmTransport {
    void postJson(const QUrl&, const QList<QPair<QByteArray, QByteArray>>&, const QByteArray&,
                  std::function<void(int, QByteArray, QString)> cb) override {
      // Answered on the next turn of the loop, as a real reply is: a synchronous answer lands
      // before the continuation's "…" card exists, and that card then never clears.
      const QJsonObject wire{{"message", QJsonObject{{"content", PLAN}}}};
      QTimer::singleShot(0, [cb, body = QJsonDocument(wire).toJson(QJsonDocument::Compact)] {
        cb(200, body, QString());
      });
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

void MainWindowGuiTest::assistantShots(MainWindow& win, const ShotSet& shots) {
  static CannedTransport canned;
  if (!shots.hasAny({QStringLiteral("assistant-docked"), QStringLiteral("assistant-floating")})) return;
  auto* canvas = win.findChild<CanvasWidget*>();
  const QImage icon(ICON_FILE);
  if (icon.isNull()) { std::printf("  assistant SKIPPED (STENCIL_DOCS_ICON_FILE unset)\n"); return; }
  // Opened as a fresh window opens it: this path keeps the blank tag the earlier page left.
  win.docSource.blankColor.clear();
  win.parts.sourceOpener.openPreviewedImageHere(icon, ICON_FILE, QString(), false, false, false, QString());
  waitUntil([canvas] { return canvas->hasImage() && canvas->getImage().width() == 512; }, 8000);
  fit(win);
  const bool real = win.settings.llmProvider == QLatin1String("stencil-server");
  if (!real) win.parts.chatAppliers.llmClient = std::make_unique<stencil::llm::LlmClient>(&canned);
  ChatDock* chat = win.chatDock;
  win.addDockWidget(Qt::LeftDockWidgetArea, chat, Qt::Horizontal);   // the edge the app opens it on
  chat->setFloating(false);
  win.acts.chat->setChecked(true);
  waitUntil([chat] { return chat->isVisible(); });
  auto* input = chat->findChild<QPlainTextEdit*>("chatInput");
  if (!input) return;
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
  if (shots.has("assistant-docked")) save("assistant-docked", &win);
  // Floating: its own window, over the right of the picture, as a user would leave it.
  if (shots.has("assistant-floating")) {
    chat->setFloating(true);
    chat->resize(400, 600);
    chat->move(win.geometry().topLeft() + QPoint(win.width() - 400 - 40, 150));
    pumpFor(300);
    fit(win);
    saveTops("assistant-floating", &win, {chat});
    chat->setFloating(false);
  }
  win.acts.chat->setChecked(false);
  pumpFor(100);
}
