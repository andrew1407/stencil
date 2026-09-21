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
#include "../../support/localPath.hpp"

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

  // A dock mid-close still reports isVisible(), and the context-menu panel is a chat surface too.
  bool MainWindow::chatSurfaceHidden() const {
    const bool dockUp = chatDock && chatDock->isVisible() && !chatClosing;
    const bool panelUp = chatMenuPanel && chatMenuPanel->isVisible();
    return !dockUp && !panelUp;
  }

  void MainWindow::showChatToast(const QString& text, bool success) {
    if (!chatToast) chatToast = new ChatToast(this);
    QString t = text;
    if (t.size() > TOAST_MAX_CHARS)
      t = t.left(TOAST_MAX_CHARS - 1).trimmed() + QChar(0x2026);
    static_cast<ChatToast*>(chatToast)->showToast(t, success, [this] {
      if (actChat) actChat->setChecked(true);
      else if (chatDock) chatDock->setVisible(true);
    });
  }

  void MainWindow::onChatStop() {
    if (!chatDock || !chatDock->isBusy()) return;
    chatStopRequested = true;
    if (llmClient) llmClient->abort();  // the canceled reply lands in onChatReply
  }

  // Offered on BOTH surfaces, so the send path is a method rather than a lambda.
  void MainWindow::chatRetryTurn(const QString& text) {
    if (!chatDock || chatDock->isBusy()) return;
    // Retry re-queues the drained attachments (latest turn only, never over something queued since).
    if (chatDock->attachedImages().isEmpty() && text == chatLastPrompt) {
      for (int i = 0; i < chatTurnAttachments.size(); ++i)
        chatDock->addAttachmentImage(
            chatTurnAttachments.at(i),
            i < chatTurnAttachmentNames.size() ? chatTurnAttachmentNames.at(i)
                                                : QString());
    }
    onChatSend(text);
  }

  // The same helper as the dock, so both bodies are literally the same string.
  QString MainWindow::withChatWarnings(const QString& text, const QStringList& warnings) {
    QString t = text;
    for (const QString& w : warnings) t += QStringLiteral("\n⚠ ") + w;
    return t;
  }

  void MainWindow::chatMirror(const QString& role, const QString& text, bool muted,
                              const QString& retryText, const QStringList& notes,
                              bool configure) {
    // Recorded whether or not the lazily-built panel exists: this log is what it replays. One append per dock row.
    chatMirrorLog.append({role, text, retryText, notes, muted});
    while (chatMirrorLog.size() > CHAT_HISTORY_BOUND) chatMirrorLog.removeFirst();
    if (chatMenuPanel)
      asChatMenu(chatMenuPanel)->appendRow(role, text, muted, retryText, false, notes,
                                            configure);
  }
  // A user-aborted turn becomes "Stopped." instead.
  void MainWindow::chatError(const QString& text, const QString& toastError) {
    const QString retryText = lastUserTurn(chatHistory);
    chatDock->appendError(text, retryText);
    chatMirror(QStringLiteral("Error"), text, true, retryText);
    if (!toastError.isEmpty() && !chatDock->isVisible())
      showChatToast(QStringLiteral("Assistant failed — %1").arg(toastError), false);
  }
  void MainWindow::chatUnreachable(const QString& text, const QString& toastError) {
    const QString retryText = lastUserTurn(chatHistory);
    chatDock->appendUnreachable(text, retryText);
    chatMirror(QStringLiteral("Error"), text, true, retryText, {}, /*configure=*/true);
    if (!toastError.isEmpty() && !chatDock->isVisible())
      showChatToast(QStringLiteral("Assistant failed — %1").arg(toastError), false);
  }
  void MainWindow::chatMirrorPending(bool show) {
    if (!chatMenuPanel) return;
    if (show) asChatMenu(chatMenuPanel)->showPending();
    else asChatMenu(chatMenuPanel)->clearPending();
  }
  void MainWindow::chatMirrorStopped(const QString& retryText) {
    if (chatMenuPanel) asChatMenu(chatMenuPanel)->markStopped(retryText);
  }
  void MainWindow::chatMirrorBusy(bool on) {
    if (chatMenuPanel) asChatMenu(chatMenuPanel)->setBusy(on);
  }
  void MainWindow::chatMirrorProviderStatus(const QString& richTooltip,
                                            ChatDock::ProviderStatus status) {
    if (chatDock) chatDock->setProviderStatus(richTooltip, status);
    if (chatMenuPanel) asChatMenu(chatMenuPanel)->setProviderStatus(richTooltip, status);
  }
  // The dock files a late note inside the last ASSISTANT bubble, else a standalone note card reported as notePosted; the panel copies that placement.
  void MainWindow::chatMirrorLateNote(const QString& text) {
    for (int i = chatMirrorLog.size() - 1; i >= 0; --i) {
      if (chatMirrorLog[i].role != QLatin1String("Assistant") || chatMirrorLog[i].muted)
        continue;
      chatMirrorLog[i].notes.append(text);
      if (chatMenuPanel) asChatMenu(chatMenuPanel)->appendLateNote(text);
      return;
    }
  }
  // Notes go to the DOCK; the mirror follows its signals, so neither surface can grow a row the other lacks.
  void MainWindow::chatLateNote(const QString& text) {
    if (chatDock) chatDock->appendLateNote(text);
  }
  void MainWindow::chatNote(const QString& text) {
    if (chatDock) chatDock->appendNote(text);
  }

  void MainWindow::chatMirrorClear() {
    chatMirrorLog.clear();
    if (chatMenuPanel) asChatMenu(chatMenuPanel)->clearRows();
  }
}  // namespace stencil::gui

