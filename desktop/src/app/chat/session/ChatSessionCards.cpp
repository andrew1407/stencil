// What onChatReply renders once the plan has parsed. Failure and stop paths: ChatSessionReply.cpp.
#include "ChatSessionController.hpp"
#include "mainWindowChatParts.hpp"
#include "planExecutor.hpp"

namespace stencil::gui {

  // History, then the ONE bubble for this turn.
  void ChatSessionController::postChatReplyBubble(const llm::OpPlan& plan, bool hasWork,
                                      bool adoptAttachment) {
    llm::ChatMessage assistant;
    assistant.role = QStringLiteral("assistant");
    assistant.text = plan.reply;  // the extracted reply, not the raw JSON (§7)
    pushChatHistory(assistant);
    QStringList notes;
    if (adoptAttachment) {
      const QString name = chatTurnAttachmentNames.value(0);
      notes << QStringLiteral("Opened %1 in the editor first — the actions ran on it.")
                   .arg(name.isEmpty() ? QStringLiteral("the attached image") : name);
    }
    // §7: a plan shaped to auto-continue gets ONE final bubble — round 1's reply is held (no card, no mirror, no persist)
    // until the continuation settles; every path where it never fires flushes the stash instead.
    const bool holdReply = hasWork && !chatContinued && chatPlanLoadsWithoutTracing(plan);
    QStringList warnings = plan.warnings;
    if (holdReply) {
      chatReplyHeld = true;
      chatHeldReply = plan.reply;
      chatHeldWarnings = plan.warnings;
      chatHeldNotes = notes;
    } else {
      if (chatReplyHeld) {   // the continuation's reply: prepend round 1's stash
        warnings = chatHeldWarnings + warnings;
        notes = chatHeldNotes + notes;
        chatReplyHeld = false;
        chatHeldReply.clear();
        chatHeldWarnings.clear();
        chatHeldNotes.clear();
      }
      chatDock->appendAssistant(plan.reply, warnings, notes);
      // The dock's shape EXACTLY, or the two transcripts drift apart.
      chatMirror(QStringLiteral("Assistant"), withChatWarnings(plan.reply, warnings), false,
                 QString(), notes);
      h.persist();  // §12: a settled turn updates the saved copy (no-op when off)
    }
  }

  // §11: option previews render AFTER the edits, through the executor's variant sandbox, so the question never touches the working image.
  void ChatSessionController::renderChatAskCard(const llm::OpPlan& plan, llm::PlanTarget& target) {
    // An option that names an image rather than a render gets no preview.
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
        FlagScope planScope(planRunning);
        const llm::ExecResult pr = llm::executePlan(previewPlan, target);
        for (int i = 0; i < pr.variants.size() && i < previewFor.size(); ++i)
          previews[previewFor[i]] = pr.variants[i].second;
      }
      chatDock->appendAsk(plan.ask, previews);
      // ANSWERED in the dock; the menu panel mirrors the question and options.
      QStringList optionLabels;
      for (const llm::AskOption& o : plan.ask.options) optionLabels << o.label;
      chatMirror(QStringLiteral("Assistant"),
                 QStringLiteral("%1\n· %2\n(answer it in the Assistant panel)")
                     .arg(plan.ask.question, optionLabels.join(QStringLiteral("\n· "))),
                 true);
    }
  }

  void ChatSessionController::renderChatVariantCards(const llm::ExecResult& res) {
    if (!res.variants.isEmpty()) {
      QVector<ChatDock::VariantCard> cards;
      bool registryChanged = false;
      for (const auto& v : res.variants) {
        ChatDock::VariantCard card;
        card.label = v.first;
        card.image = v.second;
        card.projectId = h.addVariantProject(v.second, v.first);
        registryChanged = registryChanged || !card.projectId.isEmpty();
        cards.append(card);
      }
      // ONE registry write + dock-menu rebuild for the whole reply.
      if (registryChanged) h.projectsAdded();
      chatDock->appendVariants(cards);
      // The menu row announces the variants compactly; the thumbnails live in the dock.
      chatMirror(QStringLiteral("Assistant"),
                 QStringLiteral("%1 variant(s) created as projects — open the Assistant "
                                "panel for thumbnails.")
                     .arg(cards.size()),
                 true);
      h.refreshActions();
    }
  }

}  // namespace stencil::gui
