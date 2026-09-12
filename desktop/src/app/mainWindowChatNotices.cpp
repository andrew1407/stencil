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

  // A dock that is mid-close still reports isVisible() for the length of its
  // slide, and the context-menu panel is a chat surface too — keying purely off
  // the dock's visibility swallowed the toast in both cases.
  bool MainWindow::chatSurfaceHidden() const {
    const bool dockUp = chatDock_ && chatDock_->isVisible() && !chatClosing_;
    const bool panelUp = chatMenuPanel_ && chatMenuPanel_->isVisible();
    return !dockUp && !panelUp;
  }

  void MainWindow::showChatToast(const QString& text, bool success) {
    if (!chatToast_) chatToast_ = new ChatToast(this);
    QString t = text;
    if (t.size() > kToastMaxChars)
      t = t.left(kToastMaxChars - 1).trimmed() + QChar(0x2026);
    static_cast<ChatToast*>(chatToast_)->showToast(t, success, [this] {
      // The normal open path: the checkable action shows the dock + stays in sync.
      if (actChat_) actChat_->setChecked(true);
      else if (chatDock_) chatDock_->setVisible(true);
    });
  }

  void MainWindow::onChatStop() {
    if (!chatDock_ || !chatDock_->isBusy()) return;
    chatStopRequested_ = true;
    if (llmClient_) llmClient_->abort();  // the canceled reply lands in onChatReply
  }

  // The failed/stopped turn's card offers it on BOTH surfaces, so the send path
  // is a method rather than a lambda on the dock's signal.
  void MainWindow::chatRetryTurn(const QString& text) {
    if (!chatDock_ || chatDock_->isBusy()) return;
    // Retry resends the whole turn: re-queue the drained attachments (latest
    // turn only, and never over something the user queued since).
    if (chatDock_->attachedImages().isEmpty() && text == chatLastPrompt_) {
      for (int i = 0; i < chatTurnAttachments_.size(); ++i)
        chatDock_->addAttachmentImage(
            chatTurnAttachments_.at(i),
            i < chatTurnAttachmentNames_.size() ? chatTurnAttachmentNames_.at(i)
                                                : QString());
    }
    onChatSend(text);
  }

  // The dock folds warnings into the reply's own text ("\n⚠ …"); the mirror uses
  // the same helper so both bodies are literally the same string.
  QString MainWindow::withChatWarnings(const QString& text, const QStringList& warnings) {
    QString t = text;
    for (const QString& w : warnings) t += QStringLiteral("\n⚠ ") + w;
    return t;
  }

  void MainWindow::chatMirror(const QString& role, const QString& text, bool muted,
                              const QString& retryText, const QStringList& notes,
                              bool configure) {
    // Recorded whether or not the panel exists yet: it is built lazily, and this
    // log is what it replays when it finally does. One append here per row the
    // dock displays — so the two surfaces cannot drift.
    chatMirrorLog_.append({role, text, retryText, notes, muted});
    while (chatMirrorLog_.size() > kChatHistoryBound) chatMirrorLog_.removeFirst();
    if (chatMenuPanel_)
      asChatMenu(chatMenuPanel_)->appendRow(role, text, muted, retryText, false, notes,
                                            configure);
  }
  // A user-aborted turn never lands in chatError/chatUnreachable (it becomes
  // "Stopped." instead).
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
  // The dock reports a LATE note into the reply's own bubble (appendLateNote);
  // the panel does the same, so neither grows a stray second card.
  // The dock files a late note inside the last ASSISTANT bubble (never the row
  // that happens to be last); when there is none it posts a standalone note
  // card instead, and reports THAT as notePosted. Each signal lands here, so
  // the panel copies the placement the dock chose.
  void MainWindow::chatMirrorLateNote(const QString& text) {
    for (int i = chatMirrorLog_.size() - 1; i >= 0; --i) {
      if (chatMirrorLog_[i].role != QLatin1String("Assistant") || chatMirrorLog_[i].muted)
        continue;
      chatMirrorLog_[i].notes.append(text);
      if (chatMenuPanel_) asChatMenu(chatMenuPanel_)->appendLateNote(text);
      return;
    }
  }
  // Notes go to the DOCK; the mirror follows from its notePosted/lateNotePosted
  // signals, so a note the dock posts on its own (the attachment cap) reaches
  // the panel too and neither surface can grow a row the other lacks.
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

