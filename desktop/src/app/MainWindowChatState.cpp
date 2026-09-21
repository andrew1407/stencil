#include "MainWindow.hpp"
#include "mainWindowChatParts.hpp"
#include "mainWindowHelpers.hpp"
#include "ChatMenuPanel.hpp"
#include "ChatPlanTarget.hpp"
#include "CanvasWidget.hpp"
#include "ChatDock.hpp"
#include "displayName.hpp"
#include "guiHelpers.hpp"   // confirmYesNo()
#include "iconSet.hpp"
#include "MediaLoader.hpp"
#include "Notifications.hpp"
#include "opPlan.hpp"
#include "opRegistry.hpp"   // promptText() — the §4 canon
#include "planExecutor.hpp"
#include "QtLlmTransport.hpp"
#include "RemoteSession.hpp"
#include "ServerClient.hpp"
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
    if (chatMenuAction) return;
    auto* panel = new ChatMenuPanel(
        this, [this](QString text) { onChatSend(text); }, [this] { onChatStop(); },
        // A modal cannot open under the menu's popup grab: dismiss the chain first and run a turn later.
        [this] {
          closeOpenPopupMenus();
          QTimer::singleShot(0, this, [this] {
            if (!chatDock) return;
            const int before = chatDock->attachedImages().size();
            chatDock->pickMedia();  // the dock's ONE image+video picker
            const int added = chatDock->attachedImages().size() - before;
            const QString video = chatDock->attachedVideoPath();
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
    chatMenuPanel = panel;
    chatMenuInput = panel->getInput();
    chatMenuAction = new QWidgetAction(this);
    chatMenuAction->setDefaultWidget(panel);  // takes ownership of the panel
    panel->restyle(themePalette(resolveDark(settings.themeMode), settings.accentColor));
    panel->setChatSwapSides(settings.chatSwapSides);   // the same preference the dock's row drives
    connect(panel, &ChatMenuPanel::chatSwapSidesChanged, this, [this](bool on) {
      settings.chatSwapSides = on;
      fileStore::saveSettings(settings);
      if (chatDock) chatDock->setChatSwapSides(on);
    });
    // Created LAZILY: replay what the dock DISPLAYED — never chatHistory, which carries the §7 continuation note and interim rounds.
    for (const MirrorRow& r : chatMirrorLog)
      panel->appendRow(r.role, r.text, r.muted, r.retryText, false, r.notes);
    // A turn may already be in flight from the dock.
    panel->setBusy(chatDock && chatDock->isBusy());
    if (chatDock && chatDock->isBusy()) panel->showPending();
  }

  void MainWindow::resetChatState() {
    // Everything describing THIS conversation goes, the encoded working-image cache included.
    chatHistory.clear();
    chatVideoPath.clear();
    chatVideoFrames = 0;
    chatImageDigest.clear();
    chatImageEncoded = llm::ChatImage();
    chatReplyHeld = false;   // a held round-1 bubble dies with its conversation
    chatHeldReply.clear();
    chatHeldWarnings.clear();
    chatHeldNotes.clear();
    chatTextOnlyKey.clear();  // §7: clearing the conversation re-arms the latch
    chatMirrorClear();  // the dock's trash clears BOTH views of the conversation
  }

  void MainWindow::onChatClear() {
    resetChatState();
    clearPersistedChat();  // §12.2: clearing clears the persisted copy too
  }

  // A turn is over once its plan executed and its reply is on screen (§3.0); only a §10 clearChat is left to run here.
  void MainWindow::chatTurnSettled() {
    if (!chatClearPending) return;
    chatClearPending = false;
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
    if (chatDock) chatDock->clearConversation();
    onChatClear();
  }
}  // namespace stencil::gui

