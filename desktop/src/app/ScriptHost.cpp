#include "ChatPlanTarget.hpp"
#include "MainWindow.hpp"
#include "ScriptHost.hpp"
#include "ScriptDialog.hpp"
#include "mainWindowHelpers.hpp"   // closeOpenPopupMenus()
#include "ScriptMenuPanel.hpp"
#include "Notifications.hpp"
#include "scriptFile.hpp"
#include "scriptRun.hpp"
#include "theme.hpp"

#include <QFileDialog>
#include <QPointer>
#include <QWidgetAction>

#include <memory>

// The Data section's script window and the context menu's script flyout. Neither edits the
// project itself: both run through the SAME PlanTarget the assistant's op plans drive.
// Browser twins: js/ui/script/modal.js and js/ui/ctx/script.js.
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

  void ScriptHost::openScript() {
    ScriptDialog dlg(QString(), &w);

    // Run applies the script UNDER the window and leaves it open: closing and re-exec'ing
    // read as the window flickering away, and a failure's diagnostics belong in front of you.
    QObject::connect(&dlg, &ScriptDialog::runRequested, &dlg, [this, &dlg] {
      const auto target = std::make_shared<ChatPlanTarget>(w);
      // The dialog's own parse — the one it coloured from — so a Run lexes the text once.
      runScriptThen(dlg.program(), *target,
                    [this, target, shown = QPointer<ScriptDialog>(&dlg)](const ScriptRunResult& result) {
                      reportRun(w.notify, result);
                      if (!result.isOk && shown) shown->showRunDiagnostics();
                      if (result.isOk || result.ops > 0) refreshAfterScript();   // part-ran still stands
                    });
    });
    w.execMaybePopover(dlg, w.acts.script);
  }

  // The QWidgetAction owns the panel, so the per-right-click menu rebuild can re-add it and
  // the typed script outlives the menu.
  void ScriptHost::ensureScriptMenuPanel() {
    if (w.ctxMenu.scriptAction) return;
    ScriptMenuPanel::Hooks hooks;
    // In place: the menu stays open, with the strip and the underlines on the text that ran.
    hooks.run = [this](QString text) {
      const auto target = std::make_shared<ChatPlanTarget>(w);
      runScriptThen(model::ScriptDoc::parse(text), *target,
                    [this, target](const ScriptRunResult& result) {
                      reportRun(w.notify, result);
                      if (result.ops > 0) refreshAfterScript();
                    });
    };
    // Qt closes every popup the moment a file dialog opens, native or not, so the chain is
    // dismissed deliberately and PUT BACK afterwards: the flyout is where it was, either way.
    hooks.upload = [this] {
      closeOpenPopupMenus();
      QTimer::singleShot(0, &w, [this] {
        const QString path =
            QFileDialog::getOpenFileName(&w, MainWindow::tr("Open script"), QString(), scriptFileFilter());
        QString text;
        if (path.isEmpty()) {
          w.reopenScriptFlyout();
        } else if (!readScriptFile(path, &text)) {
          if (w.notify) w.notify->error(MainWindow::tr("Could not read %1").arg(QFileInfo(path).fileName()));
          w.reopenScriptFlyout();
        } else {
          asScriptMenu(w.ctxMenu.scriptPanel)->setScript(text);
          if (w.notify)
            w.notify->info(MainWindow::tr("Loaded %1 into the script flyout").arg(QFileInfo(path).fileName()));
          w.reopenScriptFlyout();
        }
      });
    };
    hooks.download = [this] {
      const QString text = asScriptMenu(w.ctxMenu.scriptPanel)->script();
      closeOpenPopupMenus();
      QTimer::singleShot(0, &w, [this, text] {
        const QString path = QFileDialog::getSaveFileName(&w, MainWindow::tr("Save script"),
                                                          QStringLiteral("stencil.stc"),
                                                          scriptFileFilter());
        if (!path.isEmpty() && !writeScriptFile(path, text) && w.notify)
          w.notify->error(MainWindow::tr("Could not write %1").arg(QFileInfo(path).fileName()));
        w.reopenScriptFlyout();
      });
    };
    hooks.notice = [this](QString text) { if (w.notify) w.notify->success(text); };

    auto* panel = new ScriptMenuPanel(&w, std::move(hooks));
    w.ctxMenu.scriptPanel = panel;
    w.ctxMenu.scriptEditor = panel->editor();
    w.ctxMenu.scriptAction = new QWidgetAction(&w);
    w.ctxMenu.scriptAction->setDefaultWidget(panel);   // takes ownership of the panel
    panel->restyle(themePalette(resolveDark(w.settings.themeMode), w.settings.accentColor));
  }

  // A script edits the same state the toolbar does, so the same refresh follows it.
  void ScriptHost::refreshAfterScript() {
    w.refreshActions();
    w.onSelectionChanged();
    w.updateImageSizeInfo();
    w.scheduleAutosave();
  }

  // Used by a dropped .stc and by an .stc opened from the OS: run it straight away.
  void ScriptHost::runScriptFromFile(const QString& path) {
    const auto target = std::make_shared<ChatPlanTarget>(w);
    runScriptFileThen(path, *target, [this, target](const ScriptRunResult& result) {
      reportRun(w.notify, result);
      if (result.ops > 0) refreshAfterScript();
    });
  }

}  // namespace stencil::gui
