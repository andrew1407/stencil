#include "ChatSessionController.hpp"
#include "mainWindowChatParts.hpp"
#include "CanvasWidget.hpp"
#include "ChatMenuPanel.hpp"
#include "opRegistry.hpp"   // promptText() — the §4 canon
#include "ServerClient.hpp"

// The provider-status probe both chat views show, the system suffix and the §7 history bound.

namespace stencil::gui {

  void ChatSessionController::refreshLlmStatus() {
    if (!chatDock) return;
    const llm::LlmSettings cfg = h.llmSettings();
    const QString clickHint = QStringLiteral("Click to configure the assistant");
    // "assistant off" (§5): nothing probed or sent; only the Provider + Status rows (browser gearStatusRows parity).
    if (cfg.provider == QLatin1String("none")) {
      const QString rows =
          tipRow(QStringLiteral("Provider"), tipValue(QStringLiteral("None (turned off)"))) +
          tipRow(QStringLiteral("Status"),
                 tipColored(TIP_ERROR_COLOR,
                            QStringLiteral("Assistant turned off — nothing is sent anywhere")));
      chatMirrorProviderStatus(tipPanel(rows, {clickHint}),
                               ChatDock::ProviderStatus::UNREACHABLE);
      return;
    }
    const QString named = llm::llmProviderDisplayName(cfg.provider);   // the providers.json canon
    const QString provider = named.isEmpty() ? QStringLiteral("Ollama") : named;
    const QString url =
        cfg.provider == QLatin1String("stencil-server")
            ? (cfg.serverUrl.isEmpty()
                   ? QStringLiteral("(no server configured)")
                   : stencil::net::ServerClient::normalizeBase(cfg.serverUrl))
            : cfg.baseUrl;
    QString endpoint = url;  // Endpoint row drops the scheme (browser parity)
    if (endpoint.startsWith(QLatin1String("https://"), Qt::CaseInsensitive))
      endpoint.remove(0, 8);
    else if (endpoint.startsWith(QLatin1String("http://"), Qt::CaseInsensitive))
      endpoint.remove(0, 7);
    // The desktop rendering of the browser's gearStatusRows / gearTipFootText (chat/panel.js).
    const auto tooltip = [provider, endpoint](const QString& model, const QString& statusHtml,
                                              const QStringList& foot) {
      QString rows = tipRow(QStringLiteral("Provider"), tipValue(provider));
      if (!endpoint.isEmpty()) rows += tipRow(QStringLiteral("Endpoint"), tipValue(endpoint));
      rows += tipRow(QStringLiteral("Model"),
                     tipValue(model.isEmpty() ? QStringLiteral("server default") : model));
      rows += tipRow(QStringLiteral("Status"), statusHtml);
      return tipPanel(rows, foot);
    };
    const QString checking =
        tipColored(TIP_CONNECTING_COLOR, QStringLiteral("Checking the configured LLM…"));
    chatMirrorProviderStatus(tooltip(cfg.model, checking, {clickHint}),
                             ChatDock::ProviderStatus::UNKNOWN);
    // Probe only while SOMETHING shows the result.
    if (!chatDock->isVisible() && !chatMenuPanel) return;
    h.ensureClient();
    QPointer<ChatSessionController> self(this);
    const auto applyProbe = [self, tooltip, cfg, url,
                             clickHint](const llm::LlmProbeResult& r) {
      if (!self || !self->chatDock) return;
      const QString model = !r.model.isEmpty() ? r.model : cfg.model;
      QString statusHtml;
      QStringList foot;
      if (r.ok) {
        statusHtml = tipColored(
            TIP_OK_COLOR, r.detail.isEmpty()
                             ? QStringLiteral("Connected")
                             : QStringLiteral("Connected — %1").arg(r.detail));
      } else {
        statusHtml = tipColored(
            TIP_ERROR_COLOR,
            r.detail.isEmpty() ? QStringLiteral("Unreachable") : r.detail);
        foot << QStringLiteral("No LLM reachable%1 — %2configure another provider.")
                    .arg(url.isEmpty() ? QString() : QStringLiteral(" at %1").arg(url),
                         cfg.provider == QLatin1String("ollama")
                             ? QStringLiteral("start Ollama or ")
                             : QString());
      }
      foot << clickHint;
      self->chatMirrorProviderStatus(tooltip(model, statusHtml, foot),
                                     r.ok ? ChatDock::ProviderStatus::OK
                                          : ChatDock::ProviderStatus::UNREACHABLE);
    };
    // Probe cache within a TTL (browser chatSession cacheProbe parity), keyed by the effective settings.
    const QString probeKey =
        QStringList{cfg.provider, cfg.baseUrl, cfg.model, cfg.serverUrl,
                    cfg.apiKey.isEmpty() ? QString() : QStringLiteral("keyed")}
            .join(QLatin1Char('|'));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (probeKey == llmProbeKey && now - llmProbeAt <= LLM_PROBE_TTL_MS) {
      applyProbe(llmProbeCache);
      return;
    }
    llmClient->probe(cfg, [self, probeKey, applyProbe](llm::LlmProbeResult r) {
      if (!self) return;
      self->llmProbeKey = probeKey;
      self->llmProbeAt = QDateTime::currentMSecsSinceEpoch();
      self->llmProbeCache = r;
      applyProbe(r);
    });
  }

  QString ChatSessionController::chatSystemSuffix() const {
    // Wording from llm/systemPrompt.json contextSuffix*.
    const auto tpl = [](const char* key) { return llm::promptText(QLatin1String(key)); };
    QStringList parts;
    parts << (canvas->hasImage() ? tpl("contextSuffixImage")
                                        .arg(canvas->imageWidth())
                                        .arg(canvas->imageHeight())
                                  : tpl("contextSuffixNoImage"));
    if (!chatVideoPath.isEmpty())
      parts << (chatVideoFrames > 0 ? tpl("contextSuffixVideoFrames").arg(chatVideoFrames)
                                     : tpl("contextSuffixVideo"));
    return parts.join(QLatin1Char(' '));
  }

  QVector<llm::ChatMessage> ChatSessionController::wireChatMessages() const {
    // The most recent 32, with the §7 image replay rule: the current turn keeps its images, one earlier image survives.
    QVector<llm::ChatMessage> wire = chatHistory;
    trimPriorImages(wire, wire.size() - 1);
    return wire;
  }

  void ChatSessionController::pushChatHistory(const llm::ChatMessage& m) {
    // Drop prior images to what §7 would send anyway; wire output is unchanged.
    if (!m.images.isEmpty()) trimPriorImages(chatHistory, chatHistory.size());
    chatHistory.append(m);
    while (chatHistory.size() > CHAT_HISTORY_BOUND) chatHistory.removeFirst();
  }
}  // namespace stencil::gui

