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
#include "../../../support/localPath.hpp"

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

  // AI assistant glue (llm-contract.md): history + attachments (§7), the LlmClient call, op-plan execution via ChatPlanTarget.


  void MainWindow::ensureLlmClient() {
    if (llmClient) return;
    llmTransport = new llm::QtLlmTransport(this);
    llmClient = std::make_unique<llm::LlmClient>(llmTransport);
    // Prefer the LIVE connection's token (it may have been re-issued); fall back to the persisted one.
    llmClient->setServerTokenResolver([this](const QString& url) -> QString {
      if (connections)
        if (auto* c = connections->find(url)) return c->getToken();
      return llm::LlmClient::savedServerToken(url);
    });
  }

  llm::LlmSettings MainWindow::currentLlmSettings() const {
    llm::LlmSettings cfg;
    cfg.provider = settings.llmProvider;
    cfg.baseUrl = settings.llmBaseUrl.isEmpty() ? llm::defaultLlmBaseUrl(cfg.provider)
                                                 : settings.llmBaseUrl;
    cfg.model = settings.llmModel;
    cfg.apiKey = settings.llmApiKey;
    cfg.serverUrl = settings.llmServerUrl;
    // Contract §5: an empty serverUrl means the first configured connection — live first, then saved.
    if (cfg.provider == QLatin1String("stencil-server") && cfg.serverUrl.isEmpty()) {
      if (connections && !connections->urls().isEmpty()) {
        cfg.serverUrl = connections->urls().first();
      } else {
        const auto saved = stencil::net::connectionStore::loadSavedServers();
        if (!saved.isEmpty()) cfg.serverUrl = saved.first().url;
      }
    }
    return cfg;
  }


  void MainWindow::refreshLlmStatus() {
    if (!chatDock) return;
    const llm::LlmSettings cfg = currentLlmSettings();
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
    const QString provider = cfg.provider == QLatin1String("openai-compat")
                                 ? QString::fromUtf8("OpenAI API (LM Studio, vLLM, …)")
                             : cfg.provider == QLatin1String("stencil-server")
                                 ? QStringLiteral("Stencil server")
                                 : QStringLiteral("Ollama");
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
    // The desktop rendering of the browser's gearStatusRows / gearTipFootText (chatPanel.js).
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
    ensureLlmClient();
    QPointer<MainWindow> self(this);
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
        QStringList{cfg.provider, cfg.baseUrl, cfg.model, cfg.serverUrl}
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


  QString MainWindow::chatSystemSuffix() const {
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

  QVector<llm::ChatMessage> MainWindow::wireChatMessages() const {
    // The most recent 32, with the §7 image replay rule: the current turn keeps its images, one earlier image survives.
    QVector<llm::ChatMessage> wire = chatHistory;
    trimPriorImages(wire, wire.size() - 1);
    return wire;
  }

  void MainWindow::pushChatHistory(const llm::ChatMessage& m) {
    // Drop prior images to what §7 would send anyway; wire output is unchanged.
    if (!m.images.isEmpty()) trimPriorImages(chatHistory, chatHistory.size());
    chatHistory.append(m);
    while (chatHistory.size() > CHAT_HISTORY_BOUND) chatHistory.removeFirst();
  }
}  // namespace stencil::gui

