#include "mainWindow.hpp"
#include "mainWindowChatParts.hpp"
#include "mainWindowHelpers.hpp"
#include "chatMenuPanel.hpp"
#include "chatPlanTarget.hpp"
#include "canvasWidget.hpp"
#include "chatDock.hpp"
#include "displayName.hpp"
#include "guiHelpers.hpp"   // confirmYesNo()
#include "iconSet.hpp"
#include "mediaLoader.hpp"
#include "notifications.hpp"
#include "opPlan.hpp"
#include "opRegistry.hpp"   // promptText() — the §4 canon
#include "planExecutor.hpp"
#include "qtLlmTransport.hpp"
#include "remoteSession.hpp"
#include "serverClient.hpp"
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

  // AI assistant (llm-contract.md)
  // Chat glue: history + attachments (§7), the LlmClient call, and op-plan
  // execution against the live editor through ChatPlanTarget (chatPlanTarget.cpp).


  void MainWindow::ensureLlmClient() {
    if (llmClient_) return;
    llmTransport_ = new llm::QtLlmTransport(this);
    llmClient_ = std::make_unique<llm::LlmClient>(llmTransport_);
    // Prefer the LIVE connection's token (it may have been re-issued since the
    // save); fall back to the persisted one (llmClient's default behaviour).
    llmClient_->setServerTokenResolver([this](const QString& url) -> QString {
      if (connections_)
        if (auto* c = connections_->find(url)) return c->token();
      return llm::LlmClient::savedServerToken(url);
    });
  }

  llm::LlmSettings MainWindow::currentLlmSettings() const {
    llm::LlmSettings cfg;
    cfg.provider = settings_.llmProvider;
    cfg.baseUrl = settings_.llmBaseUrl.isEmpty() ? llm::defaultLlmBaseUrl(cfg.provider)
                                                 : settings_.llmBaseUrl;
    cfg.model = settings_.llmModel;
    cfg.apiKey = settings_.llmApiKey;
    cfg.serverUrl = settings_.llmServerUrl;
    // Contract §5 default: an empty serverUrl means the first configured
    // connection — live ones first, then the saved set.
    if (cfg.provider == QLatin1String("stencil-server") && cfg.serverUrl.isEmpty()) {
      if (connections_ && !connections_->urls().isEmpty()) {
        cfg.serverUrl = connections_->urls().first();
      } else {
        const auto saved = stencil::net::connectionStore::loadSavedServers();
        if (!saved.isEmpty()) cfg.serverUrl = saved.first().url;
      }
    }
    return cfg;
  }


  void MainWindow::refreshLlmStatus() {
    if (!chatDock_) return;
    const llm::LlmSettings cfg = currentLlmSettings();
    const QString clickHint = QStringLiteral("Click to configure the assistant");
    // Local-only "assistant off" (contract §5 note): nothing is probed or sent
    // anywhere; the gear table shows only the Provider + Status rows (browser
    // gearStatusRows parity for the off state).
    if (cfg.provider == QLatin1String("none")) {
      const QString rows =
          tipRow(QStringLiteral("Provider"), tipValue(QStringLiteral("None (turned off)"))) +
          tipRow(QStringLiteral("Status"),
                 tipColored(kTipErrorColor,
                            QStringLiteral("Assistant turned off — nothing is sent anywhere")));
      chatMirrorProviderStatus(tipPanel(rows, {clickHint}),
                               ChatDock::ProviderStatus::Unreachable);
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
    // Rich tooltip on the dock's gear + status dot: a table of
    // Provider / Endpoint / Model / Status rows with the status cell coloured
    // by state, then the footer lines — the desktop rendering of the browser's
    // gearStatusRows / gearTipFootText (chatPanel.js).
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
        tipColored(kTipConnectingColor, QStringLiteral("Checking the configured LLM…"));
    chatMirrorProviderStatus(tooltip(cfg.model, checking, {clickHint}),
                             ChatDock::ProviderStatus::Unknown);
    // Probe only while SOMETHING shows the result: the dock, or the context
    // menu's assistant panel (which carries the same gear + dot).
    if (!chatDock_->isVisible() && !chatMenuPanel_) return;
    ensureLlmClient();
    QPointer<MainWindow> self(this);
    const auto applyProbe = [self, tooltip, cfg, url,
                             clickHint](const llm::LlmProbeResult& r) {
      if (!self || !self->chatDock_) return;
      const QString model = !r.model.isEmpty() ? r.model : cfg.model;
      QString statusHtml;
      QStringList foot;
      if (r.ok) {
        statusHtml = tipColored(
            kTipOkColor, r.detail.isEmpty()
                             ? QStringLiteral("Connected")
                             : QStringLiteral("Connected — %1").arg(r.detail));
      } else {
        statusHtml = tipColored(
            kTipErrorColor,
            r.detail.isEmpty() ? QStringLiteral("Unreachable") : r.detail);
        foot << QStringLiteral("No LLM reachable%1 — %2configure another provider.")
                    .arg(url.isEmpty() ? QString() : QStringLiteral(" at %1").arg(url),
                         cfg.provider == QLatin1String("ollama")
                             ? QStringLiteral("start Ollama or ")
                             : QString());
      }
      foot << clickHint;
      self->chatMirrorProviderStatus(tooltip(model, statusHtml, foot),
                                     r.ok ? ChatDock::ProviderStatus::Ok
                                          : ChatDock::ProviderStatus::Unreachable);
    };
    // Reuse the last probe within a short TTL (browser chatSession
    // cacheProbe/cachedProbe parity): the menu gear and the dock dot share one
    // result instead of an HTTP probe on every right-click. Keyed by the
    // effective settings, so a config change misses the cache and re-probes.
    const QString probeKey =
        QStringList{cfg.provider, cfg.baseUrl, cfg.model, cfg.serverUrl}
            .join(QLatin1Char('|'));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (probeKey == llmProbeKey_ && now - llmProbeAt_ <= kLlmProbeTtlMs) {
      applyProbe(llmProbeCache_);
      return;
    }
    llmClient_->probe(cfg, [self, probeKey, applyProbe](llm::LlmProbeResult r) {
      if (!self) return;
      self->llmProbeKey_ = probeKey;
      self->llmProbeAt_ = QDateTime::currentMSecsSinceEpoch();
      self->llmProbeCache_ = r;
      applyProbe(r);
    });
  }


  QString MainWindow::chatSystemSuffix() const {
    // Wording from the prompt canon (llm/systemPrompt.json contextSuffix*), %1/%2 and all.
    const auto tpl = [](const char* key) { return llm::promptText(QLatin1String(key)); };
    QStringList parts;
    parts << (canvas_->hasImage() ? tpl("contextSuffixImage")
                                        .arg(canvas_->imageWidth())
                                        .arg(canvas_->imageHeight())
                                  : tpl("contextSuffixNoImage"));
    if (!chatVideoPath_.isEmpty())
      parts << (chatVideoFrames_ > 0 ? tpl("contextSuffixVideoFrames").arg(chatVideoFrames_)
                                     : tpl("contextSuffixVideo"));
    return parts.join(QLatin1Char(' '));
  }

  QVector<llm::ChatMessage> MainWindow::wireChatMessages() const {
    // Bound to the most recent 32 (chatHistory_ is already trimmed on append)
    // and apply the image replay rule: the current turn (last message) keeps
    // its images; among the earlier ones only the single most recent image
    // survives; everything else is replayed text-only (contract §7).
    QVector<llm::ChatMessage> wire = chatHistory_;
    trimPriorImages(wire, wire.size() - 1);
    return wire;
  }

  void MainWindow::pushChatHistory(const llm::ChatMessage& m) {
    // Drop prior images down to what the §7 replay rule would send anyway
    // (first image of the most recent prior image-bearer, none older) instead
    // of retaining every turn's base64 payloads. Wire output is unchanged.
    if (!m.images.isEmpty()) trimPriorImages(chatHistory_, chatHistory_.size());
    chatHistory_.append(m);
    while (chatHistory_.size() > kChatHistoryBound) chatHistory_.removeFirst();
  }
}  // namespace stencil::gui

