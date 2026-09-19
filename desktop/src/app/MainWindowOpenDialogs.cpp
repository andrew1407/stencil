// The window's dialog openers: the assistant settings the dock's gear raises, app settings, and the
// connections list.
#include "mainWindowShellParts.hpp"

namespace stencil::gui {

  // llm-contract.md §5. The gear sits in the dock's "…" menu, closed by now — so the "…" anchors the flight.
  void MainWindow::openAssistantSettings() {
    openAssistantSettingsFrom(chatDock_ ? chatDock_->moreButton() : nullptr);
  }

  void MainWindow::openAssistantSettingsFrom(QWidget* anchor, const QRect& anchorRect) {
    // A FLOATING dock is a tool window, which macOS keeps above ordinary ones; parenting to
    // it lifts the dialog to that level too, instead of playing its arrival behind the chat.
    const bool overDock = chatDock_ && chatDock_->isVisible() && chatDock_->isFloating();
    AssistantSettingsDialog dlg(settings_, overDock ? static_cast<QWidget*>(chatDock_) : this);
    wireWindowSwitching(dlg, pop_.dialogActions, actAssistantSettings_);
    support::revealDialog(dlg, anchor, anchorRect);
    if (dlg.exec() == QDialog::Accepted) applySettings(dlg.result(), true);
  }

  void MainWindow::openSettings() {
    SettingsDialog dlg(settings_, this);
    // Live-apply: every row persists itself as it changes; no Save/Cancel.
    dlg.setOnChange([this](const Settings& s) { applySettings(s, true); });
    connect(&dlg, &SettingsDialog::visualsReset, this,
            [this] { notify_->success(QStringLiteral("Visual defaults reset")); });
    execMaybePopover(dlg, actSettings_);
    // Settle-up catches a field left mid-edit; skipped when nothing changed.
    if (fileStore::settingsToJson(dlg.result()) != fileStore::settingsToJson(settings_))
      applySettings(dlg.result(), true);
  }

  void MainWindow::openConnections() {
    ConnectDialog dlg(ensureConnections(), this);
    // Sits beside Auto-connect there (browser parity).
    dlg.setSyncToServer(settings_.syncToServer);
    connect(&dlg, &ConnectDialog::syncToServerToggled, this, [this](bool on) {
      Settings s = settings_;
      s.syncToServer = on;
      applySettings(s, true);
    });
    // Reports on the toast stack, never a native alert (browser parity).
    connect(&dlg, &ConnectDialog::toast, this, [this](const QString& text, bool failed) {
      if (!notify_) return;
      if (failed) notify_->error(text); else notify_->success(text);
    });
    execMaybePopover(dlg, actConnect_);
    warnInsecureConnections();  // the dialog may have added a plaintext-remote connection
  }
}  // namespace stencil::gui
