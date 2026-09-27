#include "ChatSessionController.hpp"
#include "mainWindowChatParts.hpp"
#include "CanvasWidget.hpp"

// The turn: a Send, the §7 auto-continuation and the held round-1 bubble. The reply is in
// ChatSessionReply.cpp.

namespace stencil::gui {

  ChatSessionController::ChatSessionController(QWidget* host, CanvasWidget* canvas,
                                               ChatDock* chatDock,
                                               const QPointer<Notifications>& notify,
                                               Settings& settings,
                                               std::unique_ptr<llm::LlmClient>& llmClient,
                                               Hooks hooks)
      : QObject(host), host(host), canvas(canvas), chatDock(chatDock), notify(notify),
        settings(settings), llmClient(llmClient), h(std::move(hooks)) {}

  void ChatSessionController::onChatSend(const QString& text) {
    // Single turn at a time; the dock guards its own paths, this covers programmatic sends and
    // the window where a running plan leaves the composer live — its text goes back, not lost.
    if (chatDock->isBusy()) return;
    if (planRunning) {
      if (!chatDock->hasComposerText()) chatDock->setComposerText(text);
      return;
    }
    chatLastPrompt = text;   // a stopped turn offers this back as Retry
    // Provider "none" = assistant off: the same Configure-provider card + Retry, answered locally — nothing is sent.
    // Not routed through chatUnreachable(): `text` itself is the retry.
    if (h.llmSettings().provider == QLatin1String("none")) {
      chatDock->appendUser(text);
      chatMirror(QStringLiteral("You"), text, false);
      const QString off =
          QStringLiteral("The assistant is turned off — choose a provider to enable it.");
      chatDock->appendUnreachable(off, text);
      chatMirror(QStringLiteral("Error"), off, true, text, {}, /*configure=*/true);
      // This path never reaches onChatReply's toast/unread handling — mark it here.
      return;
    }
    h.ensureClient();
    llm::ChatMessage user;
    user.role = QStringLiteral("user");
    user.text = text;
    // A model that already rejected images this session stops receiving the auto-attached working image.
    const llm::LlmSettings llmCfg = h.llmSettings();
    const QString llmKey =
        QStringList{llmCfg.provider, llmCfg.baseUrl, llmCfg.model, llmCfg.serverUrl}
            .join(QLatin1Char('|'));
    const bool textOnlyModel = !chatTextOnlyKey.isEmpty() && chatTextOnlyKey == llmKey;
    bool edgeMapRides = false;   // §7: the edge map goes with the snapshot, wire-only
    if (!textOnlyModel && chatDock->useCurrentImage() && canvas->hasImage()) {
      // Re-encode only when the rendered pixels changed (digest match reuses the downscale + PNG + base64).
      const QImage rendered = canvas->renderToImage(/*withOverlay=*/true);
      const QByteArray digest = imageDigest(rendered);
      if (digest != chatImageDigest) {
        chatImageEncoded = encodeChatImage(rendered);
        chatEdgeMapEncoded = encodeEdgeMap(rendered);
        chatImageDigest = digest;
      }
      user.images.append(chatImageEncoded);
      edgeMapRides = true;
    }
    for (const QImage& img : chatDock->attachedImages())
      user.images.append(encodeChatImage(img));
    // USER attachments only (the working image rides every turn, §7). Kept across turns: a plan on an empty canvas adopts them.
    if (!chatDock->attachedImages().isEmpty()) {
      chatTurnAttachments = chatDock->attachedImages();
      chatTurnAttachmentNames = chatDock->attachedImageNames();
    }
    chatDock->appendUser(text, chatDock->attachedImages());
    chatMirror(QStringLiteral("You"), text, false);
    chatDock->clearAttachments();
    pushChatHistory(user);
    chatStopRequested = false;
    chatContinued = false;   // a fresh user turn re-arms the single §7 continuation
    chatDock->showPending();  // the "…" card the reply (or a stop) resolves
    chatMirrorPending(true);
    chatDock->setBusy(true);
    chatMirrorBusy(true);
    QPointer<ChatSessionController> self(this);
    const bool hadImages = !user.images.isEmpty();
    // §7 edge map: WIRE copy only, never chatHistory — never replayed or persisted.
    QVector<llm::ChatMessage> wire = wireChatMessages();
    QString suffix = chatSystemSuffix();
    if (edgeMapRides) {
      wire.last().images.insert(1, chatEdgeMapEncoded);
      suffix += QLatin1Char(' ') + QString::fromUtf8(EDGE_MAP_SUFFIX);
    }
    llmClient->chat(h.llmSettings(), wire, suffix,
                     [self, hadImages, llmKey](llm::LlmReply reply) {
                       if (!self) return;  // window closed while in flight
                       // Text-only model rejected the images: strip them from the history, remember the model, retry ONCE.
                       if (!reply.ok && hadImages && !self->chatStopRequested &&
                           reply.error.contains(QLatin1String("multimodal"), Qt::CaseInsensitive)) {
                         self->chatTextOnlyKey = llmKey;
                         for (auto& m : self->chatHistory) m.images.clear();
                         self->chatNote(QStringLiteral(
                             "This model is text-only — the image was not sent; retrying without it. "
                             "Pick a vision model to chat about the picture itself."));
                         self->llmClient->chat(
                             self->h.llmSettings(), self->wireChatMessages(),
                             self->chatSystemSuffix(), [self](llm::LlmReply retry) {
                               if (!self) return;
                               self->chatDock->setBusy(false);
                               self->chatMirrorBusy(false);
                               self->onChatReply(retry);
                             });
                         return;
                       }
                       self->chatDock->setBusy(false);
                       self->chatMirrorBusy(false);
                       self->onChatReply(reply);
                     });
  }

  // §7 shape test: continue once when the actions CONTAIN a load op and drew NO layout. Browser twin: controller.js planLoadsWithoutTracing.
  bool ChatSessionController::chatPlanLoadsWithoutTracing(const llm::OpPlan& plan) const {
    const bool loadsNew =
        std::any_of(plan.actions.cbegin(), plan.actions.cend(), [](const llm::Action& a) {
          // Incognito openUrl COUNTS: the desktop adopts incognito in place, unlike the browser's new tab.
          return a.op == llm::OpKind::OPEN_URL ||
                 a.op == llm::OpKind::BLANK || a.op == llm::OpKind::FRAME;
        });
    // A zero-line layout op validates (§2) but draws nothing — it must not count as "drew".
    const bool drewLayout = std::any_of(
        plan.actions.cbegin(), plan.actions.cend(),
        [](const llm::Action& a) { return a.op == llm::OpKind::LAYOUT && !a.lines.empty(); });
    return loadsNew && !drewLayout;
  }

  // §7 auto-continuation. True when a second round was launched.
  bool ChatSessionController::maybeContinueChat(const llm::OpPlan& plan) {
    if (chatContinued || plan.actions.isEmpty() || !chatDock || !llmClient) return false;
    if (!chatPlanLoadsWithoutTracing(plan) || !canvas->hasImage()) return false;
    const llm::LlmSettings cfg = h.llmSettings();
    const QString llmKey =
        QStringList{cfg.provider, cfg.baseUrl, cfg.model, cfg.serverUrl}.join(QLatin1Char('|'));
    if (!chatTextOnlyKey.isEmpty() && chatTextOnlyKey == llmKey) return false;

    llm::ChatMessage note;
    note.role = QStringLiteral("user");
    note.text = QString::fromUtf8(CHAT_CONTINUATION_NOTE);
    const QImage rendered = canvas->renderToImage(/*withOverlay=*/true);
    chatImageEncoded = encodeChatImage(rendered);
    chatEdgeMapEncoded = encodeEdgeMap(rendered);
    chatImageDigest = imageDigest(rendered);
    note.images.append(chatImageEncoded);
    pushChatHistory(note);
    chatContinued = true;

    chatDock->showPending();
    chatMirrorPending(true);
    chatDock->setBusy(true);
    chatMirrorBusy(true);
    QPointer<ChatSessionController> self(this);
    // §7 edge map, wire-only as in onChatSend.
    QVector<llm::ChatMessage> wire = wireChatMessages();
    wire.last().images.insert(1, chatEdgeMapEncoded);
    const QString suffix =
        chatSystemSuffix() + QLatin1Char(' ') + QString::fromUtf8(EDGE_MAP_SUFFIX);
    llmClient->chat(cfg, wire, suffix, [self](llm::LlmReply r) {
      if (!self) return;
      self->chatDock->setBusy(false);
      self->chatMirrorBusy(false);
      self->onChatReply(r);
    });
    return true;
  }

  void ChatSessionController::flushHeldChatReply() {
    if (!chatReplyHeld) return;
    chatReplyHeld = false;
    chatDock->appendAssistant(chatHeldReply, chatHeldWarnings, chatHeldNotes);
    chatMirror(QStringLiteral("Assistant"), withChatWarnings(chatHeldReply, chatHeldWarnings),
               false, QString(), chatHeldNotes);
    h.persist();
    chatHeldReply.clear();
    chatHeldWarnings.clear();
    chatHeldNotes.clear();
  }
}  // namespace stencil::gui

