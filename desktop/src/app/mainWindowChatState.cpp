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

  // Parented to the WINDOW so the per-right-click rebuild can re-add it (QWidgetAction releases, never deletes, its default widget).
  void MainWindow::ensureChatMenuPanel() {
    if (chatMenuAction_) return;
    auto* panel = new ChatMenuPanel(
        this, [this](QString text) { onChatSend(text); }, [this] { onChatStop(); },
        // A modal cannot open under the menu's popup grab: dismiss the chain first and run a turn later.
        [this] {
          closeOpenPopupMenus();
          QTimer::singleShot(0, this, [this] {
            if (!chatDock_) return;
            const int before = chatDock_->attachedImages().size();
            chatDock_->pickMedia();  // the dock's ONE image+video picker
            const int added = chatDock_->attachedImages().size() - before;
            const QString video = chatDock_->attachedVideoPath();
            // The menu is closed by now, so say what was staged in the transcript.
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
        // Captured by ChatMenuPanel BEFORE the popup closes — closeOpenPopupMenus() would hide the button first.
        [this](QRect anchorRect) {
          closeOpenPopupMenus();
          QTimer::singleShot(0, this, [this, anchorRect] {
            openAssistantSettingsFrom(nullptr, anchorRect);
          });
        },
        // Resend runs the dock's retry path, so both surfaces requeue the same attachments.
        [this](QString text) { chatRetryTurn(text); });
    chatMenuPanel_ = panel;
    chatMenuInput_ = panel->input();
    chatMenuAction_ = new QWidgetAction(this);
    chatMenuAction_->setDefaultWidget(panel);  // takes ownership of the panel
    panel->restyle(themePalette(resolveDark(settings_.themeMode), settings_.accentColor));
    panel->setChatSwapSides(settings_.chatSwapSides);   // mirrors the dock's own preference
    // Created LAZILY: replay what the dock DISPLAYED — never chatHistory_, which carries the §7 continuation note and interim rounds.
    for (const MirrorRow& r : chatMirrorLog_)
      panel->appendRow(r.role, r.text, r.muted, r.retryText, false, r.notes);
    // A turn may already be in flight from the dock.
    panel->setBusy(chatDock_ && chatDock_->isBusy());
    if (chatDock_ && chatDock_->isBusy()) panel->showPending();
  }

  void MainWindow::resetChatState() {
    // Everything describing THIS conversation goes, the encoded working-image cache included.
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
  }

  void MainWindow::onChatClear() {
    resetChatState();
    clearPersistedChat();  // §12.2: clearing clears the persisted copy too
  }

  // A turn is over once its plan executed and its reply is on screen (§3.0); only a §10 clearChat is left to run here.
  void MainWindow::chatTurnSettled() {
    if (!chatClearPending_) return;
    chatClearPending_ = false;
    // Queued, so a synchronously-settling pipeline never blocks on the modal mid-flow.
    QTimer::singleShot(0, this, &MainWindow::runDeferredChatClear);
  }

  void MainWindow::runDeferredChatClear() {
    // §10 clearChat: the MODEL-driven clear always confirms.
    if (!confirmYesNo(this, "Clear conversation",
                      "Clear this conversation? This cannot be undone.")) {
      chatLateNote(QStringLiteral("clear canceled"));   // in the last card, on both views
      return;
    }
    if (chatDock_) chatDock_->clearConversation();
    onChatClear();
  }
}  // namespace stencil::gui

