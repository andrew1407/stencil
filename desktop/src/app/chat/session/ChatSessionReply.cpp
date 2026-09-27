#include "ChatSessionController.hpp"
#include "mainWindowChatParts.hpp"
#include "CanvasWidget.hpp"
#include "planExecutor.hpp"

// A reply landing: the §7 edge map it was asked with, its plan run, and the ways a turn ends
// before one. What a parsed plan posts is in ChatSessionCards.cpp.

namespace stencil::gui {

  llm::ChatImage encodeEdgeMap(const QImage& img) {
    QImage scaled = img;
    if (std::max(img.width(), img.height()) > CHAT_IMAGE_MAX_EDGE)
      scaled = img.scaled(CHAT_IMAGE_MAX_EDGE, CHAT_IMAGE_MAX_EDGE, Qt::KeepAspectRatio,
                          Qt::SmoothTransformation);
    QImage rgba = scaled.convertToFormat(QImage::Format_RGBA8888);
    core::applyContourRGBA(rgba.bits(), rgba.width(), rgba.height());
    return encodeChatImage(rgba);
  }

  namespace {
    QString finishedText(int images, const QString& replyText) {
      return images > 0
                 ? QStringLiteral("Assistant finished (%1 images) — %2").arg(images).arg(replyText)
                 : QStringLiteral("Assistant finished — %1").arg(replyText);
    }
  }  // namespace

  void ChatSessionController::onChatReply(const llm::LlmReply& reply) {
    // A completion landing while the dock is hidden surfaces as a toast (click = open the chat).
    const bool toastWanted = chatSurfaceHidden();
    if (settleFailedChatReply(reply, toastWanted)) return;
    const llm::OpPlanResult parsed = llm::parseOpPlan(reply.text);
    if (!parsed.ok) {
      flushHeldChatReply();
      chatError(QStringLiteral("Could not read the assistant's plan: %1").arg(parsed.error),
                parsed.error);
      chatTurnSettled();
      return;
    }
    const llm::OpPlan& plan = parsed.plan;
    const bool hasWork = !plan.actions.isEmpty() || !plan.variants.isEmpty();
    const bool adoptAttachment = hasWork && !canvas->hasImage() &&
                                 !chatTurnAttachments.isEmpty() &&
                                 llm::planTouchesTheImage(plan);
    postChatReplyBubble(plan, hasWork, adoptAttachment);
    // A no-op turn still falls through to the §11 ask card. An EDITING plan on an empty canvas adopts the attachment as the working image (§7).
    if (adoptAttachment) {
      h.adoptAttachment(chatTurnAttachments.first());
    }
    const std::shared_ptr<llm::PlanTarget> target = h.planTarget();
    if (!hasWork) {   // chat-only turn: the ask card is all there is to render
      renderChatAskCard(plan, *target);
      if (toastWanted) showChatToast(finishedText(0, plan.reply), true);
      chatTurnSettled();
      return;
    }
    // §2.1: with exactly one attachment an unnamed `save` names itself after it; with several, only an `image` op decides.
    chatActiveAttachment = chatTurnAttachments.size() == 1 ? 1 : 0;
    // An awaited op answers from the event loop, so the sync timers and Send stand down until the plan's own answer.
    planRunning = true;
    QPointer<ChatSessionController> self(this);
    llm::executePlanThen(plan, *target,
                         [self, plan, target, toastWanted](const llm::ExecResult& res) {
                           if (!self) return;
                           self->planRunning = false;
                           self->onPlanDone(plan, *target, res, toastWanted);
                         });
  }

  void ChatSessionController::onPlanDone(const llm::OpPlan& plan, llm::PlanTarget& target,
                                         const llm::ExecResult& res, bool toastWanted) {
    // Skipped actions (§2.1) are reported, never silent.
    for (const QString& n : res.notes) {
      if (chatReplyHeld) {  // held turn: ride inside the eventual (one) bubble
        chatHeldNotes << n;
        continue;
      }
      chatLateNote(n);
    }
    if (res.changed) {
      h.planChanged();
    }
    if (!res.ok) {
      flushHeldChatReply();  // the reply came before the failure — post it first
      chatError(res.error, res.error);
      chatTurnSettled();
      return;
    }
    // §3.0: the lines drawn ARE the result — no self-check round. §7 auto-continuation: the plan loaded a picture the model never saw.
    if (maybeContinueChat(plan)) return;
    // The shape said "continue" but nothing launched — the held round-1 bubble posts now.
    flushHeldChatReply();
    renderChatAskCard(plan, target);
    renderChatVariantCards(res);
    if (toastWanted) showChatToast(finishedText(int(res.variants.size()), plan.reply), true);
    chatTurnSettled();
  }

  // The two ways a turn ends before a plan. True when the turn is over. Kinds mirror browser describeChatError.
  bool ChatSessionController::settleFailedChatReply(const llm::LlmReply& reply, bool toastWanted) {
    // User-aborted: the pending card becomes "Stopped." — no error card, no history push, no toast.
    if (chatStopRequested) {
      chatStopRequested = false;
      flushHeldChatReply();  // a stopped continuation must not swallow round 1's reply
      chatDock->markPendingStopped(chatLastPrompt);
      chatMirrorStopped(chatLastPrompt);
      chatTurnSettled();  // a stopped turn is over — a deferred clear still runs
      return true;
    }
    chatDock->clearPending();
    chatMirrorPending(false);
    if (!reply.ok) {
      flushHeldChatReply();  // a failed continuation must not swallow round 1's reply
      // Truncation / refusal are typed errors, never parsed as plans (§6.3). Off/Transport/Http → "unreachable" (Configure-provider CTA);
      // Disabled/Truncated → "notice" (red + Retry); Refusal/BadResponse → "Refused: "/"Error: ".
      switch (reply.failure) {
        case llm::LlmFailure::OFF:
        case llm::LlmFailure::TRANSPORT:
        case llm::LlmFailure::HTTP:
          chatUnreachable(reply.error);
          break;
        case llm::LlmFailure::DISABLED:
          // No anthropic session key (never entered, expired or forgotten): the card asks for it again.
          if (h.llmSettings().provider == QLatin1String("anthropic")) {
            const bool noKey = reply.error == QLatin1String(llm::NO_SESSION_KEY_TEXT);
            chatUnreachable(QStringLiteral("%1: %2%3.")
                                .arg(llm::llmProviderDisplayName(QStringLiteral("anthropic")), reply.error,
                                     noKey ? QStringLiteral(" — enter your key in the assistant settings") : QString()));
            break;
          }
          chatError(reply.error);
          break;
        case llm::LlmFailure::TRUNCATED:
          chatError(reply.error);
          break;
        case llm::LlmFailure::REFUSAL:
          chatError(QStringLiteral("Refused: %1").arg(reply.error));
          break;
        case llm::LlmFailure::EXPIRED: {
          // A refused SESSION, not a broken assistant: say which server and give the way back in.
          const QString retryText = lastUserTurn(chatHistory);
          chatDock->appendExpiredSession(reply.error, reply.expiredHost, retryText);
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
      return true;
    }
    return false;
  }

}  // namespace stencil::gui

