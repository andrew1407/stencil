#include "ChatPlanTarget.hpp"
#include "MainWindow.hpp"
#include "ScriptDialog.hpp"
#include "mainWindowHelpers.hpp"   // closeOpenPopupMenus()
#include "ScriptMenuPanel.hpp"
#include "Notifications.hpp"
#include "scriptFile.hpp"
#include "scriptRun.hpp"
#include "CanvasWidget.hpp"
#include "theme.hpp"

#include <QFileDialog>
#include <QFileInfo>
#include <QTimer>
#include <QWidgetAction>

// The Data section's script window and the context menu's script flyout. Neither edits the
// project itself: both run through the SAME PlanTarget the assistant's op plans drive.
// Browser twins: js/ui/scriptModal.js and js/ui/ctxScript.js.
namespace stencil::gui {

  namespace {

    // A run's verdict, said the same way wherever it was started from.
    void reportRun(Notifications* notify, const ScriptRunResult& result) {
      if (!notify) return;
      if (result.isOk) {
        notify->success(QObject::tr("Script executed successfully"));
        return;
      }
      notify->error(result.line > 0 ? QObject::tr("Script failed at line %1 — %2")
                                          .arg(result.line)
                                          .arg(result.error)
                                    : result.error);
    }

  }  // namespace

  void MainWindow::openScript() {
    ScriptDialog dlg(QString(), this);

    // Run applies the script UNDER the window and leaves it open: closing and re-exec'ing
    // read as the window flickering away, and a failure's diagnostics belong in front of you.
    connect(&dlg, &ScriptDialog::runRequested, &dlg, [this, &dlg] {
      ChatPlanTarget target(*this);
      // The dialog's own parse — the one it coloured from — so a Run lexes the text once.
      const ScriptRunResult result = runScript(dlg.program(), target);
      reportRun(notify_, result);
      if (!result.isOk) dlg.showRunDiagnostics();
      if (result.isOk || result.ops > 0) refreshAfterScript();   // part-ran still stands
    });
    execMaybePopover(dlg, actScript_);
  }

  // The QWidgetAction owns the panel, so the per-right-click menu rebuild can re-add it and
  // the typed script outlives the menu.
  void MainWindow::ensureScriptMenuPanel() {
    if (scriptMenuAction_) return;
    ScriptMenuPanel::Hooks hooks;
    // In place: the menu stays open, with the strip and the underlines on the text that ran.
    hooks.run = [this](QString text) {
      ChatPlanTarget target(*this);
      const ScriptRunResult result = runScript(text, target);
      reportRun(notify_, result);
      if (result.ops > 0) refreshAfterScript();
    };
    // Qt closes every popup the moment a file dialog opens, native or not, so the chain is
    // dismissed deliberately and PUT BACK afterwards: the flyout is where it was, either way.
    hooks.upload = [this] {
      closeOpenPopupMenus();
      QTimer::singleShot(0, this, [this] {
        const QString path =
            QFileDialog::getOpenFileName(this, tr("Open script"), QString(), scriptFileFilter());
        QString text;
        if (path.isEmpty()) {
          reopenScriptFlyout();
        } else if (!readScriptFile(path, &text)) {
          if (notify_) notify_->error(tr("Could not read %1").arg(QFileInfo(path).fileName()));
          reopenScriptFlyout();
        } else {
          asScriptMenu(scriptMenuPanel_)->setScript(text);
          if (notify_)
            notify_->info(tr("Loaded %1 into the script flyout").arg(QFileInfo(path).fileName()));
          reopenScriptFlyout();
        }
      });
    };
    hooks.download = [this] {
      const QString text = asScriptMenu(scriptMenuPanel_)->script();
      closeOpenPopupMenus();
      QTimer::singleShot(0, this, [this, text] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Save script"),
                                                          QStringLiteral("stencil.stc"),
                                                          scriptFileFilter());
        if (!path.isEmpty() && !writeScriptFile(path, text) && notify_)
          notify_->error(tr("Could not write %1").arg(QFileInfo(path).fileName()));
        reopenScriptFlyout();
      });
    };
    hooks.notice = [this](QString text) { if (notify_) notify_->success(text); };

    auto* panel = new ScriptMenuPanel(this, std::move(hooks));
    scriptMenuPanel_ = panel;
    scriptMenuEditor_ = panel->editor();
    scriptMenuAction_ = new QWidgetAction(this);
    scriptMenuAction_->setDefaultWidget(panel);   // takes ownership of the panel
    panel->restyle(themePalette(resolveDark(settings_.themeMode), settings_.accentColor));
  }

  // The chain the file dialog took down, back where it was and on the script row. Queued, so
  // the picker's own modal loop is fully unwound before the menu's begins.
  void MainWindow::reopenScriptFlyout() {
    if (contextMenuAt_.isNull() || !canvas_ || !canvas_->hasImage()) return;
    reopenScriptFlyout_ = true;
    const QPoint at = contextMenuAt_;
    QTimer::singleShot(0, this, [this, at] { showContextMenu(at); });
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
    reportRun(notify_, result);
    if (result.ops > 0) refreshAfterScript();
  }

}  // namespace stencil::gui
