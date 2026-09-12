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
}  // namespace stencil::gui

