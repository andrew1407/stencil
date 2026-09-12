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
}  // namespace stencil::gui

