// The window's dialog openers: the assistant settings the dock's gear raises, app settings, and the
// connections list.
#include "mainWindowShellParts.hpp"

namespace stencil::gui {

  // llm-contract.md §5. The gear sits in the dock's "…" menu, closed by now — so the "…" anchors the flight.
  void MainWindow::openAssistantSettings() {
    openAssistantSettingsFrom(chatDock ? chatDock->moreButton() : nullptr);
  }

  void MainWindow::openAssistantSettingsFrom(QWidget* anchor, const QRect& anchorRect) {
    // A FLOATING dock is a tool window, which macOS keeps above ordinary ones; parenting to
    // it lifts the dialog to that level too, instead of playing its arrival behind the chat.
    const bool overDock = chatDock && chatDock->isVisible() && chatDock->isFloating();
    AssistantSettingsDialog dlg(settings, overDock ? static_cast<QWidget*>(chatDock) : this);
    wireWindowSwitching(dlg, pop.dialogActions, actAssistantSettings);
    support::revealDialog(dlg, anchor, anchorRect);
    if (dlg.exec() == QDialog::Accepted) applySettings(dlg.result(), true);
  }

  void MainWindow::openSettings() {
    SettingsDialog dlg(settings, this);
    // Live-apply: every row persists itself as it changes; no Save/Cancel.
    dlg.setOnChange([this](const Settings& s) { applySettings(s, true); });
    connect(&dlg, &SettingsDialog::visualsReset, this,
            [this] { notify->success(QStringLiteral("Visual defaults reset")); });
    execMaybePopover(dlg, actSettings);
    // Settle-up catches a field left mid-edit; skipped when nothing changed.
    if (fileStore::settingsToJson(dlg.result()) != fileStore::settingsToJson(settings))
      applySettings(dlg.result(), true);
  }

  void MainWindow::openConnections() {
    ConnectDialog dlg(ensureConnections(), this);
    // Sits beside Auto-connect there (browser parity).
    dlg.setSyncToServer(settings.syncToServer);
    connect(&dlg, &ConnectDialog::syncToServerToggled, this, [this](bool on) {
      Settings s = settings;
      s.syncToServer = on;
      applySettings(s, true);
    });
    // Reports on the toast stack, never a native alert (browser parity).
    connect(&dlg, &ConnectDialog::toast, this, [this](const QString& text, bool failed) {
      if (!notify) return;
      if (failed) notify->error(text); else notify->success(text);
    });
    execMaybePopover(dlg, actConnect);
    warnInsecureConnections();  // the dialog may have added a plaintext-remote connection
  }
}  // namespace stencil::gui
