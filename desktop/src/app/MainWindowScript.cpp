#include "ChatPlanTarget.hpp"
#include "MainWindow.hpp"
#include "ScriptDialog.hpp"
#include "Notifications.hpp"
#include "scriptRun.hpp"

// The Data section's script window. The dialog only edits; running is here, through the
// SAME PlanTarget the assistant's op plans drive. Browser twin: js/ui/scriptModal.js.
namespace stencil::gui {

  void MainWindow::openScript() {
    ScriptDialog dlg(QString(), this);

    // Keep running while the user presses Run: a failed script should leave the window open
    // with its diagnostics, not vanish and make them reopen it.
    while (execMaybePopover(dlg, actScript_) == QDialog::Accepted) {
      ChatPlanTarget target(*this);
      const ScriptRunResult result = runScript(dlg.script(), target);
      if (result.ok) {
        notify_->success(result.ops == 1 ? tr("Script ran: 1 op")
                                         : tr("Script ran: %1 ops").arg(result.ops));
        refreshAfterScript();
        return;
      }

      notify_->error(result.line > 0
                         ? tr("Script failed at line %1 — %2").arg(result.line).arg(result.error)
                         : result.error);
      dlg.showRunDiagnostics();
      if (result.ops > 0) refreshAfterScript();   // whatever ran before it still stands
    }
  }

  // A script edits the same state the toolbar does, so the same refresh follows it.
  void MainWindow::refreshAfterScript() {
    refreshActions();
    onSelectionChanged();
    updateImageSizeInfo();
    scheduleAutosave();
  }

  // Used by a dropped .stc and by an .stc opened from the OS: run it straight away.
  void MainWindow::runScriptFromFile(const QString& path) {
    ChatPlanTarget target(*this);
    const ScriptRunResult result = runScriptFile(path, target);
    if (!result.ok) {
      notify_->error(result.line > 0
                         ? tr("Script failed at line %1 — %2").arg(result.line).arg(result.error)
                         : result.error);
    } else {
      notify_->success(result.ops == 1 ? tr("Script ran: 1 op")
                                       : tr("Script ran: %1 ops").arg(result.ops));
    }
    if (result.ops > 0) refreshAfterScript();
  }

}  // namespace stencil::gui
