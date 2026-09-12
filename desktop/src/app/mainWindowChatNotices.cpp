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

  // A dock mid-close still reports isVisible(), and the context-menu panel is a chat surface too.
  bool MainWindow::chatSurfaceHidden() const {
    const bool dockUp = chatDock_ && chatDock_->isVisible() && !chatClosing_;
    const bool panelUp = chatMenuPanel_ && chatMenuPanel_->isVisible();
    return !dockUp && !panelUp;
  }

  void MainWindow::showChatToast(const QString& text, bool success) {
    if (!chatToast_) chatToast_ = new ChatToast(this);
    QString t = text;
    if (t.size() > TOAST_MAX_CHARS)
      t = t.left(TOAST_MAX_CHARS - 1).trimmed() + QChar(0x2026);
    static_cast<ChatToast*>(chatToast_)->showToast(t, success, [this] {
      if (actChat_) actChat_->setChecked(true);
      else if (chatDock_) chatDock_->setVisible(true);
    });
  }

  void MainWindow::onChatStop() {
    if (!chatDock_ || !chatDock_->isBusy()) return;
    chatStopRequested_ = true;
    if (llmClient_) llmClient_->abort();  // the canceled reply lands in onChatReply
  }

  // Offered on BOTH surfaces, so the send path is a method rather than a lambda.
  void MainWindow::chatRetryTurn(const QString& text) {
    if (!chatDock_ || chatDock_->isBusy()) return;
    // Retry re-queues the drained attachments (latest turn only, never over something queued since).
    if (chatDock_->attachedImages().isEmpty() && text == chatLastPrompt_) {
      for (int i = 0; i < chatTurnAttachments_.size(); ++i)
        chatDock_->addAttachmentImage(
            chatTurnAttachments_.at(i),
            i < chatTurnAttachmentNames_.size() ? chatTurnAttachmentNames_.at(i)
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
    chatMirrorLog_.append({role, text, retryText, notes, muted});
    while (chatMirrorLog_.size() > CHAT_HISTORY_BOUND) chatMirrorLog_.removeFirst();
    if (chatMenuPanel_)
      asChatMenu(chatMenuPanel_)->appendRow(role, text, muted, retryText, false, notes,
                                            configure);
  }
  // A user-aborted turn becomes "Stopped." instead.
  void MainWindow::chatError(const QString& text, const QString& toastError) {
    const QString retryText = lastUserTurn(chatHistory_);
    chatDock_->appendError(text, retryText);
    chatMirror(QStringLiteral("Error"), text, true, retryText);
    if (!toastError.isEmpty() && !chatDock_->isVisible())
      showChatToast(QStringLiteral("Assistant failed — %1").arg(toastError), false);
  }
  void MainWindow::chatUnreachable(const QString& text, const QString& toastError) {
    const QString retryText = lastUserTurn(chatHistory_);
    chatDock_->appendUnreachable(text, retryText);
    chatMirror(QStringLiteral("Error"), text, true, retryText, {}, /*configure=*/true);
    if (!toastError.isEmpty() && !chatDock_->isVisible())
      showChatToast(QStringLiteral("Assistant failed — %1").arg(toastError), false);
  }
  void MainWindow::chatMirrorPending(bool show) {
    if (!chatMenuPanel_) return;
    if (show) asChatMenu(chatMenuPanel_)->showPending();
    else asChatMenu(chatMenuPanel_)->clearPending();
  }
  void MainWindow::chatMirrorStopped(const QString& retryText) {
    if (chatMenuPanel_) asChatMenu(chatMenuPanel_)->markStopped(retryText);
  }
  void MainWindow::chatMirrorBusy(bool on) {
    if (chatMenuPanel_) asChatMenu(chatMenuPanel_)->setBusy(on);
  }
  void MainWindow::chatMirrorProviderStatus(const QString& richTooltip,
                                            ChatDock::ProviderStatus status) {
    if (chatDock_) chatDock_->setProviderStatus(richTooltip, status);
    if (chatMenuPanel_) asChatMenu(chatMenuPanel_)->setProviderStatus(richTooltip, status);
  }
  // The dock files a late note inside the last ASSISTANT bubble, else a standalone note card reported as notePosted; the panel copies that placement.
  void MainWindow::chatMirrorLateNote(const QString& text) {
    for (int i = chatMirrorLog_.size() - 1; i >= 0; --i) {
      if (chatMirrorLog_[i].role != QLatin1String("Assistant") || chatMirrorLog_[i].muted)
        continue;
      chatMirrorLog_[i].notes.append(text);
      if (chatMenuPanel_) asChatMenu(chatMenuPanel_)->appendLateNote(text);
      return;
    }
  }
  // Notes go to the DOCK; the mirror follows its signals, so neither surface can grow a row the other lacks.
  void MainWindow::chatLateNote(const QString& text) {
    if (chatDock_) chatDock_->appendLateNote(text);
  }
  void MainWindow::chatNote(const QString& text) {
    if (chatDock_) chatDock_->appendNote(text);
  }

  void MainWindow::chatMirrorClear() {
    chatMirrorLog_.clear();
    if (chatMenuPanel_) asChatMenu(chatMenuPanel_)->clearRows();
  }
}  // namespace stencil::gui

