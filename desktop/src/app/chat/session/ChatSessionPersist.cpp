#include "ChatSessionController.hpp"
#include "mainWindowHelpers.hpp"
#include "fileStore.hpp"

// The §12 chat doc: built from the displayed conversation, and read back into both views.

namespace stencil::gui {

  // §12.1: the persisted document is the DISPLAYED conversation, not chatHistory (the model's view); it travels to every surface.
  QJsonObject ChatSessionController::buildActiveChatDoc() const {
    QJsonArray messages;
    for (const MirrorRow& r : chatMirrorLog) {
      // Muted rows and in-card notes are not conversation.
      if (r.muted) continue;
      const bool user = r.role == QLatin1String("You");
      if (!user && r.role != QLatin1String("Assistant")) continue;
      QJsonObject msg;
      msg["role"] = user ? QStringLiteral("user") : QStringLiteral("assistant");
      msg["text"] = r.text;  // the bubble's own body — images never persist (§12.1)
      messages.append(msg);
    }
    if (messages.isEmpty()) return {};
    return fileStore::buildChatDoc(messages, nowMs());
  }

  void ChatSessionController::restoreChatFromDoc(const QJsonObject& doc) {
    resetChatState();
    if (chatDock) chatDock->clearConversation();
    // parseChatDoc launders the machinery on read (§12.1); both surfaces are fed from this loop (browser chatPersistence seedHistory parity).
    const QJsonArray msgs = fileStore::parseChatDoc(doc);
    for (const auto& v : msgs) {
      const QJsonObject m = v.toObject();
      llm::ChatMessage msg;
      msg.role = m.value("role").toString();
      msg.text = m.value("text").toString();
      chatHistory.push_back(msg);
      const bool user = msg.role == QLatin1String("user");
      if (chatDock) {
        if (user) chatDock->appendUser(msg.text);
        else chatDock->appendAssistant(msg.text, {});
      }
      chatMirror(user ? QStringLiteral("You") : QStringLiteral("Assistant"), msg.text, false);
    }
  }
}  // namespace stencil::gui
