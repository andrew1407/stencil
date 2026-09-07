#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "chatMenuPanel.hpp"
#include "chatPlanTarget.hpp"
#include "canvasWidget.hpp"
#include "chatDock.hpp"
#include "displayName.hpp"
#include "guiHelpers.hpp"   // confirmYesNo()
#include "iconSet.hpp"
#include "imageFilter.hpp"
#include "mediaLoader.hpp"
#include "notifications.hpp"
#include "opPlan.hpp"
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

  // ── AI assistant (llm-contract.md) ─────────────────────────────────────
  // Chat glue: history + attachments (§7), the LlmClient call, and op-plan
  // execution against the live editor through ChatPlanTarget (chatPlanTarget.cpp).

  namespace {
    constexpr int kChatImageMaxEdge = 1568; // long-edge downscale bound (§7)

    // Downscale to ≤1568 px on the long edge and re-encode as PNG base64
    // (contract §7) — QImage::scaled keeps the aspect.
    llm::ChatImage encodeChatImage(const QImage& img) {
      QImage scaled = img;
      if (std::max(img.width(), img.height()) > kChatImageMaxEdge)
        scaled = img.scaled(kChatImageMaxEdge, kChatImageMaxEdge, Qt::KeepAspectRatio,
                            Qt::SmoothTransformation);
      llm::ChatImage out;
      out.mediaType = QStringLiteral("image/png");
      out.data = pngBytes(scaled).toBase64();
      return out;
    }

    // §7 edge map: the downscaled snapshot with the core contour filter applied
    // (the same Sobel pass the `filter` op's "contour" mode uses), encoded like
    // any attachment. RGBA8888 is the byte order applyContourRGBA expects.
    llm::ChatImage encodeEdgeMap(const QImage& img) {
      QImage scaled = img;
      if (std::max(img.width(), img.height()) > kChatImageMaxEdge)
        scaled = img.scaled(kChatImageMaxEdge, kChatImageMaxEdge, Qt::KeepAspectRatio,
                            Qt::SmoothTransformation);
      QImage rgba = scaled.convertToFormat(QImage::Format_RGBA8888);
      core::applyContourRGBA(rgba.bits(), rgba.width(), rgba.height());
      return encodeChatImage(rgba);
    }

    // Appended to the system suffix when — and only when — the edge map is
    // actually attached (llm-contract.md §7, verbatim).
    constexpr char kEdgeMapSuffix[] =
        "The second attached image is an edge-map render of the working image at the "
        "same pixel coordinates: use it to place outline points on real edges.";

    // §7 auto-continuation user-text: a message to the MODEL, never to a user.
    // It rides in chatHistory_ (and the §12 doc built from it), so every
    // display path has to filter it out — no surface may ever show it.
    constexpr char kChatContinuationNote[] =
        "[The working image is now the picture those actions loaded — continue with it.]";

    // Content digest keying the encoded working-image cache (dims included so
    // equal byte runs with different geometry can't collide).
    QByteArray imageDigest(const QImage& img) {
      QCryptographicHash h(QCryptographicHash::Sha1);
      const qint64 dims[2] = {img.width(), img.height()};
      h.addData(QByteArrayView(reinterpret_cast<const char*>(dims), sizeof(dims)));
      h.addData(QByteArrayView(reinterpret_cast<const char*>(img.constBits()),
                               static_cast<qsizetype>(img.sizeInBytes())));
      return h.result();
    }

    // The §7 image replay rule for the messages BEFORE the current turn: among
    // msgs[0..endExclusive) only the single most recent image-bearing message
    // keeps (just) its first image — the working snapshot when one rode along,
    // and never an edge map (those stay out of history entirely); every older
    // one is replayed text-only.
    void trimPriorImages(QVector<llm::ChatMessage>& msgs, qsizetype endExclusive) {
      bool newestPrior = true;
      for (qsizetype i = endExclusive - 1; i >= 0; --i) {
        auto& images = msgs[i].images;
        if (images.isEmpty()) continue;
        if (newestPrior) {
          if (images.size() > 1) {
            const llm::ChatImage keep = images.first();
            images = {keep};
          }
          newestPrior = false;
        } else {
          images.clear();
        }
      }
    }

    // ── completion toast (browser chatPanel closedToast parity) ──
    constexpr int kToastMaxChars = 90;  // shared truncation bound
    constexpr int kToastMs = 6000;      // auto-hide
    constexpr int kToastMargin = 18;    // bottom-left anchor inset

    // Bottom-left toast shown when an assistant turn finishes while the chat
    // dock is hidden: dark card, white text, success/danger accent edge.
    // Click = dismiss + run onClick (opens the chat); auto-hides after 6 s;
    // re-anchored on parent resizes via an event filter.
    class ChatToast : public QWidget {
     public:
      explicit ChatToast(QWidget* parent) : QWidget(parent) {
        setObjectName(QStringLiteral("chatToast"));
        setAttribute(Qt::WA_StyledBackground);
        setCursor(Qt::PointingHandCursor);
        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(14, 10, 14, 10);
        label_ = new QLabel(this);
        lay->addWidget(label_);
        timer_ = new QTimer(this);
        timer_->setSingleShot(true);
        QObject::connect(timer_, &QTimer::timeout, this, &QWidget::hide);
        parent->installEventFilter(this);  // keep the bottom-left anchor on resize
        hide();
      }

      void showToast(const QString& text, bool success, std::function<void()> onClick) {
        onClick_ = std::move(onClick);
        setStyleSheet(
            QStringLiteral(
                "#chatToast{background:rgba(40,46,60,242);border:1px solid rgba(255,255,255,42);"
                "border-left:3px solid %1;border-radius:8px;}"
                "#chatToast QLabel{color:white;background:transparent;}")
                .arg(QLatin1String(success ? kChatStatusOkColor : kChatStatusBadColor)));
        label_->setText(text);
        reposition();
        show();
        raise();
        timer_->start(kToastMs);
      }

     protected:
      void mousePressEvent(QMouseEvent*) override {
        timer_->stop();
        hide();
        if (onClick_) onClick_();
      }
      bool eventFilter(QObject* o, QEvent* e) override {
        if (o == parentWidget() && e->type() == QEvent::Resize && isVisible()) reposition();
        return QWidget::eventFilter(o, e);
      }

     private:
      void reposition() {
        adjustSize();
        if (QWidget* p = parentWidget())
          move(kToastMargin, p->height() - height() - kToastMargin);
      }

      QLabel* label_ = nullptr;
      QTimer* timer_ = nullptr;
      std::function<void()> onClick_;
    };

    // Every error card offers a Retry of the turn that failed: the most recent
    // USER message (pushed to the history before the request went out).
    QString lastUserTurn(const QVector<llm::ChatMessage>& history) {
      for (auto it = history.crbegin(); it != history.crend(); ++it)
        if (it->role == QLatin1String("user")) return it->text;
      return QString();
    }
  }  // namespace


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

  namespace {
    // Status-cell colours of the gear tooltip table — the browser
    // .chat-status-tip states (ok | error | connecting), readable on both the
    // light and dark QToolTip backgrounds.
    constexpr const char* kTipOkColor = "#28a745";
    constexpr const char* kTipErrorColor = "#dc3545";
    constexpr const char* kTipConnectingColor = "#e0a800";

    // Probe-cache TTL — the browser PROBE_TTL_MS (llm/chatSession.js).
    constexpr qint64 kLlmProbeTtlMs = 15000;

    QString tipColored(const char* color, const QString& text) {
      return QStringLiteral("<font color=\"%1\">%2</font>")
          .arg(QLatin1String(color), text.toHtmlEscaped());
    }

    // One .chat-status-tip table row (browser css/components.css): the label
    // cell muted, the value cell pre-rendered by the caller — tipValue()'s
    // accent shade, or the status cell's state colour.
    QString tipRow(const QString& label, const QString& valueHtml) {
      return QStringLiteral("<tr><td style=\"color:%1;\">%2&nbsp;&nbsp;&nbsp;</td><td>%3</td></tr>")
          .arg(currentPalette().textMuted.name(), label.toHtmlEscaped(), valueHtml);
    }
    QString tipValue(const QString& text) {
      return QStringLiteral("<span style=\"color:%1;\">%2</span>")
          .arg(currentPalette().textKey.name(), text.toHtmlEscaped());
    }

    // The whole tooltip: the rows table over the footer lines, the foot under a
    // hairline in smaller muted type — the browser .chat-status-tip /
    // .chat-status-tip-foot rendering (no heading; the table opens the tip).
    QString tipPanel(const QString& rows, const QStringList& foot) {
      const Palette pal = currentPalette();
      QString html =
          QStringLiteral("<table cellspacing=\"0\" cellpadding=\"1\">%1</table>").arg(rows);
      // style, not the color attribute: Qt ignores the attribute and otherwise
      // draws the rule in the TEXT colour — glaring on the dark theme (the
      // browser hairline is border-main). Verified: background-color is honoured.
      html += QStringLiteral("<hr style=\"background-color:%1;\">").arg(pal.borderMain.name());
      for (const QString& line : foot)
        html += QStringLiteral("<div style=\"color:%1; font-size:small;\">%2</div>")
                    .arg(pal.textMuted.name(), line.toHtmlEscaped());
      return html;
    }
  }  // namespace

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
    QStringList parts;
    if (canvas_->hasImage())
      parts << QStringLiteral("Current context: the working image is %1×%2 px.")
                   .arg(canvas_->imageWidth())
                   .arg(canvas_->imageHeight());
    else
      parts << QStringLiteral("Current context: there is no working image yet.");
    if (!chatVideoPath_.isEmpty())
      parts << (chatVideoFrames_ > 0
                    ? QStringLiteral("The current input is a video with about %1 frames.")
                          .arg(chatVideoFrames_)
                    : QStringLiteral("The current input is a video."));
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

  void MainWindow::onChatSend(const QString& text) {
    // Single turn at a time (the browser facade's "already answering"): the
    // dock guards its own Enter/click paths; this covers programmatic sends.
    if (chatDock_->isBusy()) return;
    chatLastPrompt_ = text;   // a stopped turn offers this back as Retry
    // Provider "none" = assistant off (browser "unreachable" kind — choosing a
    // provider IS the fix): the same Configure-provider card + Retry a Transport/
    // Http failure gets, just answered locally; nothing is encoded, recorded, or
    // sent anywhere. Not routed through chatUnreachable() (it reads chatHistory_
    // for the retry text, which this deliberately never touches) — `text` itself
    // is the retry.
    if (currentLlmSettings().provider == QLatin1String("none")) {
      chatDock_->appendUser(text);
      chatMirror(QStringLiteral("You"), text, false);
      const QString off =
          QStringLiteral("The assistant is turned off — choose a provider to enable it.");
      chatDock_->appendUnreachable(off, text);
      chatMirror(QStringLiteral("Error"), off, true, text, {}, /*configure=*/true);
      // This path never reaches onChatReply's own toast/unread handling (it
      // returns before any reply is even requested) — mark it here instead.
      return;
    }
    ensureLlmClient();
    llm::ChatMessage user;
    user.role = QStringLiteral("user");
    user.text = text;
    // A model that already rejected images this session ("multimodal not
    // supported") stops receiving the auto-attached working image — otherwise
    // EVERY turn would fail with HTTP 400 on a text-only model.
    const llm::LlmSettings llmCfg = currentLlmSettings();
    const QString llmKey =
        QStringList{llmCfg.provider, llmCfg.baseUrl, llmCfg.model, llmCfg.serverUrl}
            .join(QLatin1Char('|'));
    const bool textOnlyModel = !chatTextOnlyKey_.isEmpty() && chatTextOnlyKey_ == llmKey;
    bool edgeMapRides = false;   // §7: the edge map goes with the snapshot, wire-only
    if (!textOnlyModel && chatDock_->useCurrentImage() && canvas_->hasImage()) {
      // Re-encode the working image only when its rendered pixels changed since
      // the last turn (digest match reuses the downscale + PNG + base64).
      const QImage rendered = canvas_->renderToImage(/*withOverlay=*/true);
      const QByteArray digest = imageDigest(rendered);
      if (digest != chatImageDigest_) {
        chatImageEncoded_ = encodeChatImage(rendered);
        chatEdgeMapEncoded_ = encodeEdgeMap(rendered);
        chatImageDigest_ = digest;
      }
      user.images.append(chatImageEncoded_);
      edgeMapRides = true;
    }
    for (const QImage& img : chatDock_->attachedImages())
      user.images.append(encodeChatImage(img));
    // Show only USER attachments (the working image rides every turn, §7).
    // Kept past clearAttachments() AND across attachment-less turns: an editing
    // plan on an empty canvas adopts these as the working image (onChatReply).
    if (!chatDock_->attachedImages().isEmpty()) {
      chatTurnAttachments_ = chatDock_->attachedImages();
      chatTurnAttachmentNames_ = chatDock_->attachedImageNames();
    }
    chatDock_->appendUser(text, chatDock_->attachedImages());
    chatMirror(QStringLiteral("You"), text, false);
    chatDock_->clearAttachments();
    pushChatHistory(user);
    chatStopRequested_ = false;
    chatContinued_ = false;   // a fresh user turn re-arms the single §7 continuation
    chatDock_->showPending();  // the "…" card the reply (or a stop) resolves
    chatMirrorPending(true);
    chatDock_->setBusy(true);
    chatMirrorBusy(true);
    QPointer<MainWindow> self(this);
    const bool hadImages = !user.images.isEmpty();
    // §7 edge map: inserted into the WIRE copy of this turn only, directly
    // after the snapshot — chatHistory_ never holds it, so it is never
    // replayed or persisted, and the suffix sentence rides only with it.
    QVector<llm::ChatMessage> wire = wireChatMessages();
    QString suffix = chatSystemSuffix();
    if (edgeMapRides) {
      wire.last().images.insert(1, chatEdgeMapEncoded_);
      suffix += QLatin1Char(' ') + QString::fromUtf8(kEdgeMapSuffix);
    }
    llmClient_->chat(currentLlmSettings(), wire, suffix,
                     [self, hadImages, llmKey](llm::LlmReply reply) {
                       if (!self) return;  // window closed while in flight
                       // Text-only model rejected the attached image(s): strip
                       // every image from the history, remember the model, and
                       // retry ONCE without them — a text-only model must still
                       // be able to plan text-only edits ("make it sepia").
                       if (!reply.ok && hadImages && !self->chatStopRequested_ &&
                           reply.error.contains(QLatin1String("multimodal"), Qt::CaseInsensitive)) {
                         self->chatTextOnlyKey_ = llmKey;
                         for (auto& m : self->chatHistory_) m.images.clear();
                         self->chatNote(QStringLiteral(
                             "This model is text-only — the image was not sent; retrying without it. "
                             "Pick a vision model to chat about the picture itself."));
                         self->llmClient_->chat(
                             self->currentLlmSettings(), self->wireChatMessages(),
                             self->chatSystemSuffix(), [self](llm::LlmReply retry) {
                               if (!self) return;
                               self->chatDock_->setBusy(false);
                               self->chatMirrorBusy(false);
                               self->onChatReply(retry);
                             });
                         return;
                       }
                       self->chatDock_->setBusy(false);
                       self->chatMirrorBusy(false);
                       self->onChatReply(reply);
                     });
  }

  // §7 shape test: continue once when the actions CONTAIN a load op (openUrl
  // non-incognito / blank / frame) and drew NO layout — a plan that placed
  // lines committed to its coordinates. Browser twin: chatController.js
  // planLoadsWithoutTracing.
  bool MainWindow::chatPlanLoadsWithoutTracing(const llm::OpPlan& plan) const {
    const bool loadsNew =
        std::any_of(plan.actions.cbegin(), plan.actions.cend(), [](const llm::Action& a) {
          // Incognito openUrl COUNTS here: the desktop adopts incognito in place (same
          // canvas), so the fresh picture is this editor's image — unlike the browser,
          // where incognito opens another tab and is rightly excluded.
          return a.op == llm::OpKind::OpenUrl ||
                 a.op == llm::OpKind::Blank || a.op == llm::OpKind::Frame;
        });
    // A zero-line layout op validates (§2) but draws nothing — it must not
    // count as "drew" or it suppresses the very round meant to draw the lines.
    const bool drewLayout = std::any_of(
        plan.actions.cbegin(), plan.actions.cend(),
        [](const llm::Action& a) { return a.op == llm::OpKind::Layout && !a.lines.empty(); });
    return loadsNew && !drewLayout;
  }

  // §7 auto-continuation. True when a second round was launched (the caller stops).
  bool MainWindow::maybeContinueChat(const llm::OpPlan& plan) {
    if (chatContinued_ || plan.actions.isEmpty() || !chatDock_ || !llmClient_) return false;
    if (!chatPlanLoadsWithoutTracing(plan) || !canvas_->hasImage()) return false;
    const llm::LlmSettings cfg = currentLlmSettings();
    const QString llmKey =
        QStringList{cfg.provider, cfg.baseUrl, cfg.model, cfg.serverUrl}.join(QLatin1Char('|'));
    // A text-only model has nothing to continue WITH.
    if (!chatTextOnlyKey_.isEmpty() && chatTextOnlyKey_ == llmKey) return false;

    llm::ChatMessage note;
    note.role = QStringLiteral("user");
    note.text = QString::fromUtf8(kChatContinuationNote);
    const QImage rendered = canvas_->renderToImage(/*withOverlay=*/true);
    chatImageEncoded_ = encodeChatImage(rendered);
    chatEdgeMapEncoded_ = encodeEdgeMap(rendered);
    chatImageDigest_ = imageDigest(rendered);
    note.images.append(chatImageEncoded_);
    pushChatHistory(note);
    chatContinued_ = true;

    chatDock_->showPending();
    chatMirrorPending(true);
    chatDock_->setBusy(true);
    chatMirrorBusy(true);
    QPointer<MainWindow> self(this);
    // §7 edge map, same wire-only rules as onChatSend: rides directly after
    // the fresh snapshot with the sentence, never enters chatHistory_.
    QVector<llm::ChatMessage> wire = wireChatMessages();
    wire.last().images.insert(1, chatEdgeMapEncoded_);
    const QString suffix =
        chatSystemSuffix() + QLatin1Char(' ') + QString::fromUtf8(kEdgeMapSuffix);
    llmClient_->chat(cfg, wire, suffix, [self](llm::LlmReply r) {
      if (!self) return;
      self->chatDock_->setBusy(false);
      self->chatMirrorBusy(false);
      self->onChatReply(r);
    });
    return true;
  }

  void MainWindow::flushHeldChatReply() {
    if (!chatReplyHeld_) return;
    chatReplyHeld_ = false;
    chatDock_->appendAssistant(chatHeldReply_, chatHeldWarnings_, chatHeldNotes_);
    chatMirror(QStringLiteral("Assistant"), withChatWarnings(chatHeldReply_, chatHeldWarnings_),
               false, QString(), chatHeldNotes_);
    persistActiveChat();
    chatHeldReply_.clear();
    chatHeldWarnings_.clear();
    chatHeldNotes_.clear();
  }

  // A dock that is mid-close still reports isVisible() for the length of its
  // slide, and the context-menu panel is a chat surface too — keying purely off
  // the dock's visibility swallowed the toast in both cases.
  bool MainWindow::chatSurfaceHidden() const {
    const bool dockUp = chatDock_ && chatDock_->isVisible() && !chatClosing_;
    const bool panelUp = chatMenuPanel_ && chatMenuPanel_->isVisible();
    return !dockUp && !panelUp;
  }

  void MainWindow::showChatToast(const QString& text, bool success) {
    if (!chatToast_) chatToast_ = new ChatToast(this);
    QString t = text;
    if (t.size() > kToastMaxChars)
      t = t.left(kToastMaxChars - 1).trimmed() + QChar(0x2026);
    static_cast<ChatToast*>(chatToast_)->showToast(t, success, [this] {
      // The normal open path: the checkable action shows the dock + stays in sync.
      if (actChat_) actChat_->setChecked(true);
      else if (chatDock_) chatDock_->setVisible(true);
    });
  }

  void MainWindow::onChatStop() {
    if (!chatDock_ || !chatDock_->isBusy()) return;
    chatStopRequested_ = true;
    if (llmClient_) llmClient_->abort();  // the canceled reply lands in onChatReply
  }

  // The context menu's assistant row: built once, parented to the WINDOW so the
  // per-right-click menu rebuild can re-add it without losing the transcript
  // (QWidgetAction releases — never deletes — a default widget when the menu it
  // was in goes away).
  void MainWindow::ensureChatMenuPanel() {
    if (chatMenuAction_) return;
    auto* panel = new ChatMenuPanel(
        this, [this](QString text) { onChatSend(text); }, [this] { onChatStop(); },
        // Attach and the gear both lead to a modal dialog, which cannot open
        // under the menu's popup grab (a native file dialog fights it outright).
        // Both therefore dismiss the menu chain first and run a turn later, once
        // exec() has returned and the grab is gone.
        [this] {
          closeOpenPopupMenus();
          QTimer::singleShot(0, this, [this] {
            if (!chatDock_) return;
            const int before = chatDock_->attachedImages().size();
            chatDock_->pickMedia();  // the dock's ONE image+video picker
            const int added = chatDock_->attachedImages().size() - before;
            const QString video = chatDock_->attachedVideoPath();
            // The menu is closed by now, so say what was staged in the
            // transcript — it rides along with the next turn either way.
            if (added > 0 || !video.isEmpty()) {
              QStringList parts;
              if (added > 0) parts << QStringLiteral("%1 image(s)").arg(added);
              if (!video.isEmpty()) parts << QStringLiteral("1 video");
              chatMirror(QStringLiteral("Attached"),
                         parts.join(QStringLiteral(" + ")) +
                             QStringLiteral(" — sent with your next message"),
                         true);
            }
          });
        },
        // `anchorRect` is the gear's (or a card's "Configure provider" CTA's)
        // global rect, captured by ChatMenuPanel BEFORE this popup starts
        // closing — closeOpenPopupMenus() would otherwise hide the button first,
        // leaving the dialog to fall back to the dock's "…" trigger instead of
        // the control that was actually clicked.
        [this](QRect anchorRect) {
          closeOpenPopupMenus();
          QTimer::singleShot(0, this, [this, anchorRect] {
            openAssistantSettingsFrom(nullptr, anchorRect);
          });
        },
        // Resend from an error/stopped card here runs the dock's retry path, so
        // the two surfaces requeue the same attachments and send one turn.
        [this](QString text) { chatRetryTurn(text); });
    chatMenuPanel_ = panel;
    chatMenuInput_ = panel->input();
    chatMenuAction_ = new QWidgetAction(this);
    chatMenuAction_->setDefaultWidget(panel);  // takes ownership of the panel
    panel->restyle(themePalette(resolveDark(settings_.themeMode), settings_.accentColor));
    panel->setChatSwapSides(settings_.chatSwapSides);   // mirrors the dock's own preference
    // The panel is created LAZILY, so a conversation may already exist (chatted
    // in the dock, then opened the menu). It replays what the dock DISPLAYED —
    // never chatHistory_, which is the model's view: that carries the §7
    // continuation note ("[The working image is now …]") and every interim
    // round's reply, none of which the dock shows and neither should this.
    for (const MirrorRow& r : chatMirrorLog_)
      panel->appendRow(r.role, r.text, r.muted, r.retryText, false, r.notes);
    // A turn may already be in flight (started from the dock) — open in the
    // right mode rather than showing a stale send button.
    panel->setBusy(chatDock_ && chatDock_->isBusy());
    if (chatDock_ && chatDock_->isBusy()) panel->showPending();
  }

  // The failed/stopped turn's card offers it on BOTH surfaces, so the send path
  // is a method rather than a lambda on the dock's signal.
  void MainWindow::chatRetryTurn(const QString& text) {
    if (!chatDock_ || chatDock_->isBusy()) return;
    // Retry resends the whole turn: re-queue the drained attachments (latest
    // turn only, and never over something the user queued since).
    if (chatDock_->attachedImages().isEmpty() && text == chatLastPrompt_) {
      for (int i = 0; i < chatTurnAttachments_.size(); ++i)
        chatDock_->addAttachmentImage(
            chatTurnAttachments_.at(i),
            i < chatTurnAttachmentNames_.size() ? chatTurnAttachmentNames_.at(i)
                                                : QString());
    }
    onChatSend(text);
  }

  // The dock folds warnings into the reply's own text ("\n⚠ …"); the mirror uses
  // the same helper so both bodies are literally the same string.
  QString MainWindow::withChatWarnings(const QString& text, const QStringList& warnings) {
    QString t = text;
    for (const QString& w : warnings) t += QStringLiteral("\n⚠ ") + w;
    return t;
  }

  void MainWindow::chatMirror(const QString& role, const QString& text, bool muted,
                              const QString& retryText, const QStringList& notes,
                              bool configure) {
    // Recorded whether or not the panel exists yet: it is built lazily, and this
    // log is what it replays when it finally does. One append here per row the
    // dock displays — so the two surfaces cannot drift.
    chatMirrorLog_.append({role, text, retryText, notes, muted});
    while (chatMirrorLog_.size() > kChatHistoryBound) chatMirrorLog_.removeFirst();
    if (chatMenuPanel_)
      asChatMenu(chatMenuPanel_)->appendRow(role, text, muted, retryText, false, notes,
                                            configure);
  }
  // A user-aborted turn never lands in chatError/chatUnreachable (it becomes
  // "Stopped." instead).
  void MainWindow::chatError(const QString& text, const QString& toastError) {
    const QString retryText = lastUserTurn(chatHistory_);
    chatDock_->appendError(text, retryText);
    chatMirror(QStringLiteral("Error"), text, true, retryText);
    if (!toastError.isEmpty() && !chatDock_->isVisible())
      showChatToast(QStringLiteral("Assistant failed — %1").arg(toastError), false);
  }
  void MainWindow::chatUnreachable(const QString& text, const QString& toastError) {
    const QString retryText = lastUserTurn(chatHistory_);
    chatDock_->appendUnreachable(text, retryText);
    chatMirror(QStringLiteral("Error"), text, true, retryText, {}, /*configure=*/true);
    if (!toastError.isEmpty() && !chatDock_->isVisible())
      showChatToast(QStringLiteral("Assistant failed — %1").arg(toastError), false);
  }
  void MainWindow::chatMirrorPending(bool show) {
    if (!chatMenuPanel_) return;
    if (show) asChatMenu(chatMenuPanel_)->showPending();
    else asChatMenu(chatMenuPanel_)->clearPending();
  }
  void MainWindow::chatMirrorStopped(const QString& retryText) {
    if (chatMenuPanel_) asChatMenu(chatMenuPanel_)->markStopped(retryText);
  }
  void MainWindow::chatMirrorBusy(bool on) {
    if (chatMenuPanel_) asChatMenu(chatMenuPanel_)->setBusy(on);
  }
  void MainWindow::chatMirrorProviderStatus(const QString& richTooltip,
                                            ChatDock::ProviderStatus status) {
    if (chatDock_) chatDock_->setProviderStatus(richTooltip, status);
    if (chatMenuPanel_) asChatMenu(chatMenuPanel_)->setProviderStatus(richTooltip, status);
  }
  // The dock reports a LATE note into the reply's own bubble (appendLateNote);
  // the panel does the same, so neither grows a stray second card.
  // The dock files a late note inside the last ASSISTANT bubble (never the row
  // that happens to be last); when there is none it posts a standalone note
  // card instead, and reports THAT as notePosted. Each signal lands here, so
  // the panel copies the placement the dock chose.
  void MainWindow::chatMirrorLateNote(const QString& text) {
    for (int i = chatMirrorLog_.size() - 1; i >= 0; --i) {
      if (chatMirrorLog_[i].role != QLatin1String("Assistant") || chatMirrorLog_[i].muted)
        continue;
      chatMirrorLog_[i].notes.append(text);
      if (chatMenuPanel_) asChatMenu(chatMenuPanel_)->appendLateNote(text);
      return;
    }
  }
  // Notes go to the DOCK; the mirror follows from its notePosted/lateNotePosted
  // signals, so a note the dock posts on its own (the attachment cap) reaches
  // the panel too and neither surface can grow a row the other lacks.
  void MainWindow::chatLateNote(const QString& text) {
    if (chatDock_) chatDock_->appendLateNote(text);
  }
  void MainWindow::chatNote(const QString& text) {
    if (chatDock_) chatDock_->appendNote(text);
  }

  void MainWindow::chatMirrorClear() {
    chatMirrorLog_.clear();
    if (chatMenuPanel_) asChatMenu(chatMenuPanel_)->clearRows();
  }

  void MainWindow::resetChatState() {
    // Everything that describes THIS conversation goes: the replayed history,
    // the attached video input (and its frame estimate, which feeds the system
    // suffix), and the encoded working-image cache — the next turn re-encodes
    // from scratch rather than replaying a digest tied to the old thread.
    chatHistory_.clear();
    chatVideoPath_.clear();
    chatVideoFrames_ = 0;
    chatImageDigest_.clear();
    chatImageEncoded_ = llm::ChatImage();
    chatReplyHeld_ = false;   // a held round-1 bubble dies with its conversation
    chatHeldReply_.clear();
    chatHeldWarnings_.clear();
    chatHeldNotes_.clear();
    chatTextOnlyKey_.clear();  // §7: clearing the conversation re-arms the latch
    chatMirrorClear();  // the dock's trash clears BOTH views of the conversation
    // Provider settings, the working image, and the canvas are untouched.
  }

  void MainWindow::onChatClear() {
    resetChatState();
    clearPersistedChat();  // §12.2: clearing clears the persisted copy too
  }

  // A turn is over the moment its plan has executed and its reply is on screen
  // (§3.0 — nothing runs after it). The toast/unread badge fire on the reply path
  // itself; all this terminal still owns is a §10 clearChat the plan asked for.
  void MainWindow::chatTurnSettled() {
    if (!chatClearPending_) return;
    chatClearPending_ = false;
    // Queued, so a synchronously-settling pipeline never blocks on the modal
    // mid-flow — the confirm shows once the turn's call stack unwinds.
    QTimer::singleShot(0, this, &MainWindow::runDeferredChatClear);
  }

  void MainWindow::runDeferredChatClear() {
    // §10 clearChat: the trash button clears silently, but the MODEL-driven
    // clear always confirms — the in-app confirm keeps the user in the loop.
    if (!confirmYesNo(this, "Clear conversation",
                      "Clear this conversation? This cannot be undone.")) {
      chatLateNote(QStringLiteral("clear canceled"));   // in the last card, on both views
      return;
    }
    // The trash-button flow: dock transcript, then history + persisted copy.
    if (chatDock_) chatDock_->clearConversation();
    onChatClear();
  }

  // §12.1: the persisted document is the DISPLAYED conversation, not chatHistory_
  // (the model's view, which carries the §7 continuation note and every interim
  // round's reply). The doc travels with the project to every other surface, so
  // internal plumbing written here cannot be filtered out again there.
  QJsonObject MainWindow::buildActiveChatDoc() const {
    QJsonArray messages;
    for (const MirrorRow& r : chatMirrorLog_) {
      // Muted rows are errors/notices/attachment chatter, not conversation; the
      // in-card notes are executor asides, not the reply text.
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
      // Server-linked session: the chat lives on the server (kind "chat", §9).
      // Fire-and-forget like the video upload — a failed push costs nothing but
      // the server copy; the conversation itself is unaffected.
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
    // parseChatDoc launders the machinery on read (§12.1: the §7 continuation
    // note and raw plans never come back from storage) — what is left reads as
    // conversation, on screen AND in the model's replay history (browser
    // chatPersistence parity: seedHistory gets the same filtered list). Both
    // surfaces are fed from this single loop, so they cannot disagree.
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

  void MainWindow::onChatReply(const llm::LlmReply& reply) {
    // A completion landing while the dock is hidden surfaces as a bottom-left
    // toast (click = open the chat) instead of vanishing silently; an open
    // dock changes nothing.
    const bool toastWanted = chatSurfaceHidden();
    // User-aborted turn: the pending card becomes "Stopped." — no error card,
    // no assistant history push, no toast (stopping requires the open dock).
    if (chatStopRequested_) {
      chatStopRequested_ = false;
      flushHeldChatReply();  // a stopped continuation must not swallow round 1's reply
      chatDock_->markPendingStopped(chatLastPrompt_);
      chatMirrorStopped(chatLastPrompt_);
      chatTurnSettled();  // a stopped turn is over — a deferred clear still runs
      return;
    }
    chatDock_->clearPending();
    chatMirrorPending(false);
    if (!reply.ok) {
      flushHeldChatReply();  // a failed continuation must not swallow round 1's reply
      // Truncation / refusal are typed errors — never parsed as plans (§6.3).
      // Kinds mirror browser describeChatError: Off/Transport/Http are its
      // "unreachable" (card + Configure-provider CTA, the config IS the fix);
      // Disabled/Truncated are its "notice" (red + Retry, raw message, no CTA —
      // the provider is fine, just not answering this way); Refusal/BadResponse
      // are its "refusal"/generic "error" (red + Retry, "Refused: "/"Error: ").
      switch (reply.failure) {
        case llm::LlmFailure::Off:
        case llm::LlmFailure::Transport:
        case llm::LlmFailure::Http:
          chatUnreachable(reply.error);
          break;
        case llm::LlmFailure::Truncated:
        case llm::LlmFailure::Disabled:
          chatError(reply.error);
          break;
        case llm::LlmFailure::Refusal:
          chatError(QStringLiteral("Refused: %1").arg(reply.error));
          break;
        case llm::LlmFailure::Expired: {
          // A refused SESSION, not a broken assistant: say which server and give
          // the way back in. Mirrored to the menu panel like any other error.
          const QString retryText = lastUserTurn(chatHistory_);
          chatDock_->appendExpiredSession(reply.error, reply.expiredHost, retryText);
          chatMirror(QStringLiteral("Error"), reply.error, true, retryText);
          break;
        }
        default:  // BadResponse and anything untyped
          chatError(QStringLiteral("Error: %1").arg(reply.error));
          break;
      }
      if (toastWanted)
        showChatToast(QStringLiteral("Assistant failed — %1").arg(reply.error), false);
      chatTurnSettled();
      return;
    }
    const llm::OpPlanResult parsed = llm::parseOpPlan(reply.text);
    if (!parsed.ok) {
      flushHeldChatReply();
      chatError(QStringLiteral("Could not read the assistant's plan: %1").arg(parsed.error),
                parsed.error);
      chatTurnSettled();
      return;
    }
    const llm::OpPlan& plan = parsed.plan;
    llm::ChatMessage assistant;
    assistant.role = QStringLiteral("assistant");
    assistant.text = plan.reply;  // the extracted reply, not the raw JSON (§7)
    pushChatHistory(assistant);
    // Decide the attachment adoption (see below) BEFORE the reply renders: its
    // note rides inside the SAME assistant bubble, as muted text — one bubble
    // per turn, and the danger style stays reserved for actual failures.
    const bool hasWork = !plan.actions.isEmpty() || !plan.variants.isEmpty();
    const bool adoptAttachment = hasWork && !canvas_->hasImage() &&
                                 !chatTurnAttachments_.isEmpty() &&
                                 llm::planTouchesTheImage(plan);
    QStringList notes;
    if (adoptAttachment) {
      const QString name = chatTurnAttachmentNames_.value(0);
      notes << QStringLiteral("Opened %1 in the editor first — the actions ran on it.")
                   .arg(name.isEmpty() ? QStringLiteral("the attached image") : name);
    }
    // §7: a plan shaped to auto-continue gets ONE final bubble (browser parity) —
    // hold round 1's reply (no card, no mirror, no persist) until the continuation
    // settles; the final round folds the stash into ITS bubble, and every path
    // where the continuation never fires flushes the stash instead.
    const bool holdReply = hasWork && !chatContinued_ && chatPlanLoadsWithoutTracing(plan);
    QStringList warnings = plan.warnings;
    if (holdReply) {
      chatReplyHeld_ = true;
      chatHeldReply_ = plan.reply;
      chatHeldWarnings_ = plan.warnings;
      chatHeldNotes_ = notes;
    } else {
      if (chatReplyHeld_) {   // the continuation's reply: prepend round 1's stash
        warnings = chatHeldWarnings_ + warnings;
        notes = chatHeldNotes_ + notes;
        chatReplyHeld_ = false;
        chatHeldReply_.clear();
        chatHeldWarnings_.clear();
        chatHeldNotes_.clear();
      }
      chatDock_->appendAssistant(plan.reply, warnings, notes);
      // The dock's shape EXACTLY: warnings folded into the reply's own text,
      // executor notes as muted lines inside the same bubble. Mirroring them any
      // other way is how the two transcripts drifted apart.
      chatMirror(QStringLiteral("Assistant"), withChatWarnings(plan.reply, warnings), false,
                 QString(), notes);
      persistActiveChat();  // §12: a settled turn updates the saved copy (no-op when off)
    }
    const auto finishedText = [](int images, const QString& replyText) {
      return images > 0
                 ? QStringLiteral("Assistant finished (%1 images) — %2")
                       .arg(images)
                       .arg(replyText)
                 : QStringLiteral("Assistant finished — %1").arg(replyText);
    };
    // A no-op turn still falls through to the §11 ask card below (asking INSTEAD
    // of acting is what §11 is for). An EDITING plan on an empty canvas adopts
    // the just-attached picture as the working image (§7; browser planEditsTheImage).
    if (adoptAttachment) {
      // The same entry the .stencil / server paths use to adopt a bare QImage; an empty
      // layout means "just the picture", which is exactly what an attachment is.
      loadImageWithLayout(chatTurnAttachments_.first(), QJsonObject());
      playImageArrival();   // it lands on the canvas like any other fresh image
    }
    ChatPlanTarget target(*this);
    llm::ExecResult res;
    if (hasWork) {
      // §2.1: with exactly one attachment THAT is what the plan works on, so an
      // unnamed `save` can name itself after it without an `image` op; with
      // several, only an `image` op decides (browser parity).
      chatActiveAttachment_ = chatTurnAttachments_.size() == 1 ? 1 : 0;
      res = llm::executePlan(plan, target);
      // Skipped actions (§2.1: an attachment this turn cannot satisfy, a save
      // with nothing loaded) are reported, never silent.
      for (const QString& n : res.notes) {
        if (chatReplyHeld_) {  // held turn: ride inside the eventual (one) bubble
          chatHeldNotes_ << n;
          continue;
        }
        chatLateNote(n);
      }
      if (res.changed) {
        refreshActions();
        onSelectionChanged();
        updateImageSizeInfo();
        scheduleAutosave();
      }
      if (!res.ok) {
        flushHeldChatReply();  // the reply came before the failure — post it first
        chatError(res.error, res.error);
        chatTurnSettled();
        return;
      }
      // §3.0: the lines the model drew ARE the result. There is no self-check round
      // and no re-trace at zoom — the turn is over once the plan has executed.
      // §7 auto-continuation: the plan loaded a picture the model never saw (and
      // drew no layout). Re-send once with the new working image attached.
      if (maybeContinueChat(plan)) return;
      // The shape said "continue" but nothing launched (empty canvas, text-only
      // model) — the held round-1 bubble posts right now, nothing is lost.
      flushHeldChatReply();
    }
    // §11: the plan may also ASK. The option previews are rendered AFTER the edits, so they
    // show the choice against the image as it now is — through the executor's own variant
    // sandbox, so the working image is untouched by the question itself. An option that
    // names an image rather than a render gets no preview here (the card shows its label).
    if (!plan.ask.options.isEmpty()) {
      llm::OpPlan previewPlan;
      previewPlan.reply = plan.reply;   // unused by the executor; kept non-empty for clarity
      QVector<int> previewFor;          // which option each rendered variant belongs to
      for (int i = 0; i < plan.ask.options.size(); ++i) {
        if (plan.ask.options[i].actions.isEmpty()) continue;
        llm::Variant v;
        v.label = plan.ask.options[i].label;
        v.actions = plan.ask.options[i].actions;
        previewPlan.variants.push_back(std::move(v));
        previewFor.push_back(i);
      }
      QVector<QImage> previews(plan.ask.options.size());
      if (!previewPlan.variants.isEmpty()) {
        const llm::ExecResult pr = llm::executePlan(previewPlan, target);
        // A preview that fails costs that option its picture, never the card.
        for (int i = 0; i < pr.variants.size() && i < previewFor.size(); ++i)
          previews[previewFor[i]] = pr.variants[i].second;
      }
      chatDock_->appendAsk(plan.ask, previews);
      // The card is ANSWERED in the dock (radios, previews, Submit); the menu
      // panel mirrors the question and its options so the turn reads whole
      // there too — the same split the variant cards take.
      QStringList optionLabels;
      for (const llm::AskOption& o : plan.ask.options) optionLabels << o.label;
      chatMirror(QStringLiteral("Assistant"),
                 QStringLiteral("%1\n· %2\n(answer it in the Assistant panel)")
                     .arg(plan.ask.question, optionLabels.join(QStringLiteral("\n· "))),
                 true);
    }
    if (!hasWork) {   // chat-only turn: the ask card above was all there was to render
      if (toastWanted) showChatToast(finishedText(0, plan.reply), true);
      chatTurnSettled();
      return;
    }
    if (!res.variants.isEmpty()) {
      QVector<ChatDock::VariantCard> cards;
      bool registryChanged = false;
      for (const auto& v : res.variants) {
        ChatDock::VariantCard card;
        card.label = v.first;
        card.image = v.second;
        card.projectId = addImageProjectEntry(v.second, v.first, /*deferRegistrySave=*/true);
        registryChanged = registryChanged || !card.projectId.isEmpty();
        cards.append(card);
      }
      // ONE registry write + dock-menu rebuild for the whole reply, however
      // many variants it created (each entry deferred its own save above).
      if (registryChanged) {
        fileStore::saveProjects(projectList_);
        refreshDockMenu();
      }
      chatDock_->appendVariants(cards);
      // The menu row announces the variants compactly — the thumbnails with
      // their Open / Save buttons live in the dock, which this same reply just
      // populated (nothing is dropped, only rendered once).
      chatMirror(QStringLiteral("Assistant"),
                 QStringLiteral("%1 variant(s) created as projects — open the Assistant "
                                "panel for thumbnails.")
                     .arg(cards.size()),
                 true);
      refreshActions();
    }
    if (toastWanted) showChatToast(finishedText(res.variants.size(), plan.reply), true);
    chatTurnSettled();
  }

  void MainWindow::onChatVideoAttached(const QString& path) {
    chatVideoPath_ = path;
    chatVideoFrames_ = 0;
    if (!chatMedia_) chatMedia_ = new MediaLoader(this);
    // Extract the first frame as an image attachment (videos are NEVER sent to
    // the LLM — contract §7) and remember the timeline for the system suffix.
    QPointer<MainWindow> self(this);
    chatMedia_->extractFrames(path, {0}, [self](QList<QImage> frames, QString err) {
      if (!self) return;
      if (!err.isEmpty()) {
        self->notify_->error(QStringLiteral("Video preview failed: %1").arg(err));
        return;
      }
      self->chatVideoFrames_ = self->chatMedia_->frameCount();
      if (!frames.isEmpty()) self->chatDock_->addAttachmentImage(frames.first());
      self->notify_->info(
          QStringLiteral("Video frame attached — the video itself is never sent"));
    });
    offerChatVideoUpload(path);
  }

  void MainWindow::offerChatVideoUpload(const QString& path) {
    // Optional server storage: offer uploading the video bytes to the linked
    // project with kind "video" (contract §8; uploadFileAsync is kind-generic).
    const auto& link = remoteSession_->link();
    if (link.address.isEmpty() || !connections_) return;
    auto* c = connections_->find(link.address);
    if (!c) return;
    const QString host = QUrl(link.address).host();
    if (!confirmYesNo(this, "Upload video",
                      QStringLiteral("Store this video with the shared project on %1?").arg(host)))
      return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
      notify_->error(QStringLiteral("Could not read the video file"));
      return;
    }
    const QByteArray bytes = f.readAll();
    QString ext = QFileInfo(path).suffix().toLower();
    if (ext.isEmpty()) ext = QStringLiteral("mp4");
    QPointer<MainWindow> self(this);
    c->uploadFileAsync(link.id, QStringLiteral("video"), bytes, ext, 0, 0,
                       [self](bool ok) {
                         if (!self) return;
                         if (ok) self->notify_->success("Video uploaded to the server");
                         else self->notify_->error("Video upload failed");
                       });
  }

  bool MainWindow::chatExtractFrames(const QVector<int>& indices, QString* err) {
    if (chatVideoPath_.isEmpty()) {
      if (err) *err = QStringLiteral("frame: no video attached");
      return false;
    }
    if (!chatMedia_) chatMedia_ = new MediaLoader(this);
    // Sequential seeks are async; block on a local event loop (the MediaLoader
    // timeout backstops a stuck seek) so the plan executes in order. `finished`
    // guards the synchronous-failure path (e.g. the file vanished): quitting a
    // loop that never started would otherwise leave exec() running forever.
    QEventLoop loop;
    QList<QImage> frames;
    QString error;
    bool finished = false;
    chatMedia_->extractFrames(chatVideoPath_, QList<int>(indices.begin(), indices.end()),
                              [&](QList<QImage> f, QString e) {
                                frames = std::move(f);
                                error = std::move(e);
                                finished = true;
                                loop.quit();
                              });
    if (!finished) loop.exec();
    if (!error.isEmpty()) {
      if (err) *err = QStringLiteral("frame: %1").arg(error);
      return false;
    }
    bool registryChanged = false;
    for (int i = 0; i < frames.size(); ++i)
      registryChanged =
          !addImageProjectEntry(frames.at(i), QStringLiteral("frame %1").arg(indices.at(i)),
                                /*deferRegistrySave=*/true)
               .isEmpty() ||
          registryChanged;
    // ONE registry write + dock-menu rebuild for the whole extraction.
    if (registryChanged) {
      fileStore::saveProjects(projectList_);
      refreshDockMenu();
    }
    refreshActions();
    notify_->success(QStringLiteral("Opened %1 video frame(s) as projects").arg(frames.size()));
    return true;
  }

  QString MainWindow::addImageProjectEntry(const QImage& img, const QString& baseName,
                                           bool deferRegistrySave) {
    if (img.isNull() || incognito_) return QString();  // incognito never persists
    Project pr;
    pr.meta.id = projectsStore_.createId(nowMs(), makeSalt());
    // Unique-ify the name against the local list ("crop", "crop 2", …) with
    // the shared collision rules (checkProjectName → core validateName).
    QString name = baseName.isEmpty() ? QStringLiteral("variant") : baseName;
    {
      QString candidate = name;
      int i = 2;
      while (!checkProjectName(candidate, QString()).ok)
        candidate = QStringLiteral("%1 %2").arg(name).arg(i++);
      name = candidate;
    }
    pr.meta.name = name.toStdString();
    pr.meta.createdAt = pr.meta.updatedAt = nowMs();
    pr.meta.expiresAt = core::ProjectsStore::addPeriod(pr.meta.updatedAt,
                                                       core::ProjectsStore::DEFAULT_PERIOD);
    // Persist the pixels like createLocalProject does for pathless canvases.
    const QString imgDir = fileStore::stateDir() + "/images";
    QDir().mkpath(imgDir);
    const QString path = imgDir + "/" + QString::fromStdString(pr.meta.id) + ".png";
    if (!img.save(path, "PNG")) return QString();
    pr.imagePath = path;
    pr.meta.hasImage = true;
    pr.meta.imageW = img.width();
    pr.meta.imageH = img.height();
    projectList_.push_back(pr);
    if (!deferRegistrySave) {
      fileStore::saveProjects(projectList_);
      refreshDockMenu();
    }
    return QString::fromStdString(pr.meta.id);
  }

  // ── §2.1 `save` ─────────────────────────────────────────────────────────────

  QString MainWindow::uniqueLocalProjectName(const QString& wanted) const {
    std::vector<core::ProjectMeta> metas;
    for (const auto& p : projectList_) metas.push_back(p.meta);
    core::ProjectsStore store;   // local; never disturbs projectsStore_
    store.load(metas);
    if (!store.nameExists(wanted.toStdString())) return wanted;
    for (int n = 2; n < 1000; ++n) {
      const QString candidate = QStringLiteral("%1 %2").arg(wanted).arg(n);
      if (!store.nameExists(candidate.toStdString())) return candidate;
    }
    return wanted;
  }

  QString MainWindow::chatSaveBaseName(const QString& requested) const {
    QString name = requested.trimmed();
    // The attachment the plan is working on names it — a 3-image plan then
    // leaves 3 distinctly named projects.
    if (name.isEmpty() && chatActiveAttachment_ >= 1)
      name = QFileInfo(chatTurnAttachmentNames_.value(chatActiveAttachment_ - 1))
                 .completeBaseName()
                 .trimmed();
    if (name.isEmpty()) name = activeProjectName();
    if (name.isEmpty() && canvas_ && canvas_->hasImage()) name = canvas_->imageBaseName();
    if (name.isEmpty()) name = QStringLiteral("Untitled");
    return name.left(80);   // core validateName's cap
  }

  bool MainWindow::chatSaveProject(const QString& name, const QString& dest, QString* err) {
    if (!canvas_->hasImage()) {   // the executor notes this case before calling
      if (err) *err = QStringLiteral("save: there is no image to save");
      return false;
    }
    // Incognito blocks what the app would write BY ITSELF, not what the user asks for: writing
    // a file to a path they named is the Save Image… they can already do from the toolbar, and
    // a pathless save promotes the session out of incognito (below) instead of refusing.
    // §10: a destination the user named — a .stencil bundle, an image file, or a folder
    // (which gets "<name>.png", the format Save-as offers). Anything else stays the
    // editor's own project store, exactly as before.
    if (!dest.isEmpty()) {
      const QString base = chatSaveBaseName(name);
      QString path = support::expandHomePath(dest);
      const bool isFile = !support::fileExtensionOf(path).isEmpty();
      if (!isFile) {
        QDir().mkpath(path);
        if (path.endsWith(QLatin1Char('/'))) path.chop(1);
        path += QStringLiteral("/") + base + QStringLiteral(".png");
      }
      if (path.endsWith(QStringLiteral(".stencil"), Qt::CaseInsensitive)) {
        if (!writeFileBytes(path, buildStencilBytes())) {
          if (err) *err = QStringLiteral("save: could not write %1").arg(path);
          return false;
        }
      } else if (!canvas_->renderToImage(true).save(path)) {
        if (err) *err = QStringLiteral("save: could not write %1").arg(path);
        return false;
      }
      notify_->success(QStringLiteral("Saved %1").arg(support::shortName(path)));
      return true;
    }
    if (incognito_) {   // leaving incognito IS the save the user asked for
      const QString promoted = promoteIncognitoToLocal(chatSaveBaseName(name));
      notify_->success(QStringLiteral("Left incognito — saved \"%1\"")
                           .arg(support::shortName(promoted)));
      return true;
    }
    // A FRESH project per save (the create-project path, minus its dialogs), so
    // each image of a multi-image plan lands as its own project.
    const QString unique = uniqueLocalProjectName(chatSaveBaseName(name));
    createLocalProject(unique, /*announce=*/false);
    notify_->success(QStringLiteral("Saved \"%1\"").arg(support::shortName(unique)));
    return true;
  }

  // §10 openFile: a user-named local file, dispatched by extension the way /upload does in
  // the console — a project restores everything, a layout draws onto the current picture,
  // and a picture/video goes through the same awaited source load openUrl uses.
  bool MainWindow::chatOpenFile(const QString& raw, QString* err) {
    const QString path = support::expandHomePath(raw);
    if (!QFileInfo::exists(path)) {
      if (err) *err = QStringLiteral("openFile: %1 does not exist").arg(path);
      return false;
    }
    if (path.endsWith(QStringLiteral(".stencil"), Qt::CaseInsensitive)) {
      openProjectFile(path);
      return true;
    }
    if (path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
      if (!canvas_->hasImage()) {
        if (err) *err = QStringLiteral("openFile: load a picture before applying a layout");
        return false;
      }
      applyLayoutFromSource(path);
      return true;
    }
    notify_->info(QStringLiteral("Opening %1").arg(support::shortName(path)));
    QString why;
    if (chatLoadSource(path, incognito_, &why)) return true;
    if (err) *err = QStringLiteral("openFile: %1").arg(why);
    return false;
  }

  // Open `src` (a URL or a local path) here and BLOCK until MediaLoader resolves — the plan
  // executor awaits its loads so the next action edits the new picture, not the old one.
  bool MainWindow::chatLoadSource(const QString& src, bool incognito, QString* why) {
    constexpr int kSourceWaitMs = 20000;  // finite: a stalled load can't hang the plan
    ensureMediaLoader();  // before OUR connects, so the canvas adopts first
    QEventLoop loop;
    bool loaded = false, failed = false;
    const auto cLoaded = QObject::connect(mediaLoader_, &MediaLoader::loaded, &loop,
                                          [&](const QImage&, const QString&) {
                                            loaded = true;
                                            loop.quit();
                                          });
    const auto cFailed = QObject::connect(mediaLoader_, &MediaLoader::failed, &loop,
                                          [&](const QString& msg) {
                                            failed = true;
                                            if (why) *why = msg;
                                            loop.quit();
                                          });
    QTimer::singleShot(kSourceWaitMs, &loop, [&loop] { loop.quit(); });
    openSourceHere(src, 0, incognito);
    if (!loaded && !failed) loop.exec();  // guards a synchronous outcome
    QObject::disconnect(cLoaded);
    QObject::disconnect(cFailed);
    if (failed) return false;
    if (!loaded) {
      if (why) *why = QStringLiteral("timed out loading %1").arg(src);
      return false;
    }
    return true;
  }

}  // namespace stencil::gui
