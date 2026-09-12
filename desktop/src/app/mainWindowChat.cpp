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
    const bool adoptAttachment = hasWork && !canvas_->hasImage() &&
                                 !chatTurnAttachments_.isEmpty() &&
                                 llm::planTouchesTheImage(plan);
    postChatReplyBubble(plan, hasWork, adoptAttachment);
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
    renderChatAskCard(plan, target);
    if (!hasWork) {   // chat-only turn: the ask card above was all there was to render
      if (toastWanted) showChatToast(finishedText(0, plan.reply), true);
      chatTurnSettled();
      return;
    }
    renderChatVariantCards(res, plan.reply);
    if (toastWanted) showChatToast(finishedText(res.variants.size(), plan.reply), true);
    chatTurnSettled();
  }

  // The two ways a turn ends before a plan: the user stopped it, or the provider failed.
  // True when the turn is over — the caller returns at once. Kinds mirror browser
  // describeChatError: Off/Transport/Http are its "unreachable" (card + Configure-provider
  // CTA, since the config IS the fix), Disabled/Truncated its red "notice" with Retry.
  bool MainWindow::settleFailedChatReply(const llm::LlmReply& reply, bool toastWanted) {
    // User-aborted turn: the pending card becomes "Stopped." — no error card,
    // no assistant history push, no toast (stopping requires the open dock).
    if (chatStopRequested_) {
      chatStopRequested_ = false;
      flushHeldChatReply();  // a stopped continuation must not swallow round 1's reply
      chatDock_->markPendingStopped(chatLastPrompt_);
      chatMirrorStopped(chatLastPrompt_);
      chatTurnSettled();  // a stopped turn is over — a deferred clear still runs
      return true;
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
      return true;
    }
    return false;
  }

}  // namespace stencil::gui

