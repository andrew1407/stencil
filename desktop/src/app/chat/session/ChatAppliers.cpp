#include "MainWindow.hpp"
#include "SharedState.hpp"
#include "ChatAppliers.hpp"
#include "LlmClient.hpp"
#include "SessionKey.hpp"
#include "QtLlmTransport.hpp"
#include "ServerClient.hpp"
#include "ChatSessionController.hpp"
#include "RemoteSession.hpp"

// The window's side of the assistant (llm-contract.md): its LlmClient and the settings it runs
// with, both derived from the window's Settings and live connections, and where the §12 chat doc
// is filed — the linked server project's "chat" file, else the active local project.

namespace stencil::gui {

  ChatAppliers::ChatAppliers(MainWindow& w) : w(w) {}
  ChatAppliers::~ChatAppliers() = default;

  void ChatAppliers::ensureLlmClient() {
    if (llmClient) return;
    llmTransport = new llm::QtLlmTransport(&w);
    llmClient = std::make_unique<llm::LlmClient>(llmTransport);
    // Prefer the LIVE connection's token (it may have been re-issued); fall back to the persisted one.
    llmClient->setServerTokenResolver([this](const QString& url) -> QString {
      if (w.remote.conns())
        if (auto* c = w.remote.conns()->find(url)) return c->getToken();
      return llm::LlmClient::savedServerToken(url);
    });
  }

  llm::LlmSettings ChatAppliers::currentLlmSettings() const {
    llm::LlmSettings cfg;
    cfg.provider = w.settings.llmProvider;
    cfg.baseUrl = w.settings.llmBaseUrl.isEmpty() ? llm::defaultLlmBaseUrl(cfg.provider)
                                                 : w.settings.llmBaseUrl;
    cfg.model = w.settings.llmModel;
    // anthropic reads the session key, never a stored one; an expired key is dropped by this read.
    cfg.apiKey = cfg.provider == QLatin1String("anthropic") ? llm::SessionKey::instance().key()
                                                            : w.settings.llmApiKey;
    cfg.serverUrl = w.settings.llmServerUrl;
    // Contract §5: an empty serverUrl means the first configured connection — live first, then saved.
    if (cfg.provider == QLatin1String("stencil-server") && cfg.serverUrl.isEmpty()) {
      if (w.remote.conns() && !w.remote.conns()->urls().isEmpty()) {
        cfg.serverUrl = w.remote.conns()->urls().first();
      } else {
        const auto saved = stencil::net::connectionStore::loadSavedServers();
        if (!saved.isEmpty()) cfg.serverUrl = saved.first().url;
      }
    }
    return cfg;
  }

  void ChatAppliers::persistActiveChat() {
    if (!w.settings.saveChatsWithProject || w.projectHost.incognito) return;
    const QJsonObject doc = w.chatSession->buildActiveChatDoc();
    const auto& link = w.remote.session->getLink();
    if (!link.address.isEmpty()) {
      // Server-linked: the chat lives on the server (kind "chat", §9). Fire-and-forget; a failed push costs only the server copy.
      if (auto* c = w.remote.conns() ? w.remote.conns()->find(link.address) : nullptr) {
        if (doc.isEmpty())
          c->deleteFileAsync(link.id, QStringLiteral("chat"), [](bool) {});
        else
          c->uploadFileAsync(link.id, QStringLiteral("chat"),
                             QJsonDocument(doc).toJson(QJsonDocument::Compact),
                             QStringLiteral("json"), 0, 0, [](bool) {});
      }
      return;
    }
    if (w.projectHost.activeProjectId.isEmpty()) return;  // temporary editor — nowhere to file it
    Project* pr = w.projectHost.findProject(w.projectHost.activeProjectId.toStdString());
    if (!pr) return;
    pr->chat = doc;
    SharedState::instance().saveProjects(&w);
  }

  void ChatAppliers::clearPersistedChat() {
    if (!w.settings.saveChatsWithProject || w.projectHost.incognito) return;
    const auto& link = w.remote.session->getLink();
    if (!link.address.isEmpty()) {
      if (auto* c = w.remote.conns() ? w.remote.conns()->find(link.address) : nullptr)
        c->deleteFileAsync(link.id, QStringLiteral("chat"), [](bool) {});
      return;
    }
    if (w.projectHost.activeProjectId.isEmpty()) return;
    Project* pr = w.projectHost.findProject(w.projectHost.activeProjectId.toStdString());
    if (!pr || pr->chat.isEmpty()) return;
    pr->chat = QJsonObject();
    SharedState::instance().saveProjects(&w);
  }

}  // namespace stencil::gui
