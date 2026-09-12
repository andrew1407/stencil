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
#include "imageFilter.hpp"
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

  llm::ChatImage encodeEdgeMap(const QImage& img) {
    QImage scaled = img;
    if (std::max(img.width(), img.height()) > kChatImageMaxEdge)
      scaled = img.scaled(kChatImageMaxEdge, kChatImageMaxEdge, Qt::KeepAspectRatio,
                          Qt::SmoothTransformation);
    QImage rgba = scaled.convertToFormat(QImage::Format_RGBA8888);
    core::applyContourRGBA(rgba.bits(), rgba.width(), rgba.height());
    return encodeChatImage(rgba);
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
}  // namespace stencil::gui

