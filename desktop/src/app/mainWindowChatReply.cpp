// What onChatReply renders once the plan has parsed. Failure and stop paths: mainWindowChat.cpp.
#include "mainWindow.hpp"
#include "chatDock.hpp"
#include "chatPlanTarget.hpp"
#include "fileStore.hpp"
#include "opPlan.hpp"
#include "planExecutor.hpp"
#include <QImage>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

namespace stencil::gui {

  // History, then the ONE bubble for this turn.
  void MainWindow::postChatReplyBubble(const llm::OpPlan& plan, bool hasWork,
                                      bool adoptAttachment) {
    llm::ChatMessage assistant;
    assistant.role = QStringLiteral("assistant");
    assistant.text = plan.reply;  // the extracted reply, not the raw JSON (§7)
    pushChatHistory(assistant);
    QStringList notes;
    if (adoptAttachment) {
      const QString name = chatTurnAttachmentNames_.value(0);
      notes << QStringLiteral("Opened %1 in the editor first — the actions ran on it.")
                   .arg(name.isEmpty() ? QStringLiteral("the attached image") : name);
    }
    // §7: a plan shaped to auto-continue gets ONE final bubble — round 1's reply is held (no card, no mirror, no persist)
    // until the continuation settles; every path where it never fires flushes the stash instead.
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
      // The dock's shape EXACTLY, or the two transcripts drift apart.
      chatMirror(QStringLiteral("Assistant"), withChatWarnings(plan.reply, warnings), false,
                 QString(), notes);
      persistActiveChat();  // §12: a settled turn updates the saved copy (no-op when off)
    }
  }

  // §11: option previews render AFTER the edits, through the executor's variant sandbox, so the question never touches the working image.
  void MainWindow::renderChatAskCard(const llm::OpPlan& plan, llm::PlanTarget& target) {
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
        const llm::ExecResult pr = llm::executePlan(previewPlan, target);
        for (int i = 0; i < pr.variants.size() && i < previewFor.size(); ++i)
          previews[previewFor[i]] = pr.variants[i].second;
      }
      chatDock_->appendAsk(plan.ask, previews);
      // ANSWERED in the dock; the menu panel mirrors the question and options.
      QStringList optionLabels;
      for (const llm::AskOption& o : plan.ask.options) optionLabels << o.label;
      chatMirror(QStringLiteral("Assistant"),
                 QStringLiteral("%1\n· %2\n(answer it in the Assistant panel)")
                     .arg(plan.ask.question, optionLabels.join(QStringLiteral("\n· "))),
                 true);
    }
  }

  void MainWindow::renderChatVariantCards(const llm::ExecResult& res, const QString& reply) {
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
      // ONE registry write + dock-menu rebuild for the whole reply.
      if (registryChanged) {
        fileStore::saveProjects(projectList_);
        refreshDockMenu();
      }
      chatDock_->appendVariants(cards);
      // The menu row announces the variants compactly; the thumbnails live in the dock.
      chatMirror(QStringLiteral("Assistant"),
                 QStringLiteral("%1 variant(s) created as projects — open the Assistant "
                                "panel for thumbnails.")
                     .arg(cards.size()),
                 true);
      refreshActions();
    }
  }

}  // namespace stencil::gui
