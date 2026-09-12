#include "MainWindow.hpp"
#include "mainWindowChatParts.hpp"
#include "mainWindowHelpers.hpp"
#include "ChatMenuPanel.hpp"
#include "ChatPlanTarget.hpp"
#include "CanvasWidget.hpp"
#include "ChatDock.hpp"
#include "displayName.hpp"
#include "guiHelpers.hpp"   // confirmYesNo()
#include "iconSet.hpp"
#include "MediaLoader.hpp"
#include "Notifications.hpp"
#include "opPlan.hpp"
#include "opRegistry.hpp"   // promptText() — the §4 canon
#include "planExecutor.hpp"
#include "QtLlmTransport.hpp"
#include "RemoteSession.hpp"
#include "ServerClient.hpp"
#include "connectionStore.hpp"
#include "theme.hpp"
#include "tipContent.hpp"   // currentPalette() — the colours rich tooltips are drawn in
#include "../support/localPath.hpp"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QToolButton>
#include <QWidgetAction>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <algorithm>

namespace stencil::gui {

  // §12.1: the persisted document is the DISPLAYED conversation, not chatHistory_ (the model's view); it travels to every surface.
  QJsonObject MainWindow::buildActiveChatDoc() const {
    QJsonArray messages;
    for (const MirrorRow& r : chatMirrorLog_) {
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

  void MainWindow::persistActiveChat() {
    if (!settings_.saveChatsWithProject || incognito_) return;
    const QJsonObject doc = buildActiveChatDoc();
    const auto& link = remoteSession_->link();
    if (!link.address.isEmpty()) {
      // Server-linked: the chat lives on the server (kind "chat", §9). Fire-and-forget; a failed push costs only the server copy.
      if (auto* c = connections_ ? connections_->find(link.address) : nullptr) {
        if (doc.isEmpty())
          c->deleteFileAsync(link.id, QStringLiteral("chat"), [](bool) {});
        else
          c->uploadFileAsync(link.id, QStringLiteral("chat"),
                             QJsonDocument(doc).toJson(QJsonDocument::Compact),
                             QStringLiteral("json"), 0, 0, [](bool) {});
      }
      return;
    }
    if (activeProjectId_.isEmpty()) return;  // temporary editor — nowhere to file it
    Project* pr = findProject(activeProjectId_.toStdString());
    if (!pr) return;
    pr->chat = doc;
    fileStore::saveProjects(projectList_);
  }

  void MainWindow::restoreChatFromDoc(const QJsonObject& doc) {
    resetChatState();
    if (chatDock_) chatDock_->clearConversation();
    // parseChatDoc launders the machinery on read (§12.1); both surfaces are fed from this loop (browser chatPersistence seedHistory parity).
    const QJsonArray msgs = fileStore::parseChatDoc(doc);
    for (const auto& v : msgs) {
      const QJsonObject m = v.toObject();
      llm::ChatMessage msg;
      msg.role = m.value("role").toString();
      msg.text = m.value("text").toString();
      chatHistory_.push_back(msg);
      const bool user = msg.role == QLatin1String("user");
      if (chatDock_) {
        if (user) chatDock_->appendUser(msg.text);
        else chatDock_->appendAssistant(msg.text, {});
      }
      chatMirror(user ? QStringLiteral("You") : QStringLiteral("Assistant"), msg.text, false);
    }
  }

  void MainWindow::clearPersistedChat() {
    if (!settings_.saveChatsWithProject || incognito_) return;
    const auto& link = remoteSession_->link();
    if (!link.address.isEmpty()) {
      if (auto* c = connections_ ? connections_->find(link.address) : nullptr)
        c->deleteFileAsync(link.id, QStringLiteral("chat"), [](bool) {});
      return;
    }
    if (activeProjectId_.isEmpty()) return;
    Project* pr = findProject(activeProjectId_.toStdString());
    if (!pr || pr->chat.isEmpty()) return;
    pr->chat = QJsonObject();
    fileStore::saveProjects(projectList_);
  }
}  // namespace stencil::gui

