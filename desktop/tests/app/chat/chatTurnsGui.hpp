#pragma once
// The chat-turn suites' common ground: a sized window, the assistant on ollama answered by a mock
// (the window's private settings and client are passed in by the suite, its friend), the dock
// opened by its own action, and the transcript as a reader sees it.
#include "../../MainWindow.gui.hpp"
#include <QFrame>
#include <QLabel>

namespace stencil::guitest {

  inline bool showSized(MainWindow& win, int width, int height) {
    win.resize(width, height);
    win.show();
    return QTest::qWaitForWindowExposed(&win);
  }

  // ollama's response shape around a reply's JSON text.
  inline QByteArray ollamaReply(const QString& json) {
    return QJsonDocument(QJsonObject{{"message", QJsonObject{{"content", json}}}})
        .toJson(QJsonDocument::Compact);
  }

  // The assistant on ollama, every POST answered synchronously by `mock`.
  inline void useOllama(gui::Settings& settings, std::unique_ptr<llm::LlmClient>& client,
                        MockChatTransport& mock) {
    settings.llmProvider = "ollama";
    settings.llmBaseUrl = "http://localhost:11434";
    client = std::make_unique<llm::LlmClient>(&mock);
  }

  // Opened through its own action, and settled past the open slide; null if either is missing.
  inline gui::ChatDock* openChatDock(MainWindow& win) {
    auto* chat = win.findChild<QAction*>("actChat");
    auto* dock = qobject_cast<gui::ChatDock*>(win.findChild<QDockWidget*>("llmChatDock"));
    if (!chat || !dock) return nullptr;
    chat->setChecked(true);
    (void)QTest::qWaitFor([dock] { return dock->isVisible() && dock->width() > 200; }, 5000);
    return dock;
  }

  // Cards are the only direct QFrame children of the transcript column (the suggestion block is
  // a plain QWidget).
  inline qsizetype cardCount(QWidget* transcript) {
    return transcript->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly).size();
  }

  // Every card's body and in-card notes on `surface`, the displayed transcript; a pending "…"
  // card is not one yet.
  inline QStringList displayedCards(QWidget* surface) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QStringList out;
    for (QFrame* f : surface->findChildren<QFrame*>()) {
      const QString kind = f->objectName();
      if (!kind.startsWith(QLatin1String("chatCard")) || kind == QLatin1String("chatCardMore")) continue;
      QStringList texts;
      for (QLabel* l : f->findChildren<QLabel*>()) {
        const QString b = l->property("chatBody").toString();
        const QString n = l->property("chatNote").toString();
        if (!b.isEmpty()) texts << b;
        else if (!n.isEmpty()) texts << n;
      }
      if (texts.size() == 1 && texts.first() == QStringLiteral("…")) continue;
      out << kind + QStringLiteral(": ") + texts.join(QStringLiteral(" ¶ "));
    }
    return out;
  }

}  // namespace stencil::guitest
