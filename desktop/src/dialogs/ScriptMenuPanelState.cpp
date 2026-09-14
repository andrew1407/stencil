#include "ScriptMenuPanel.hpp"

#include "ScriptEditorWidget.hpp"
#include "../support/iconSet.hpp"
#include "../support/theme.hpp"
#include "scriptMenuPanelParts.hpp"

#include <QPushButton>

// The script flyout's behaviour: what it reports, what it gates, and how a theme flip re-inks it.
namespace stencil::gui {

  void ScriptMenuPanel::showRunDiagnostics() { edit_->showRunDiagnostics(); }

  // Run, Copy and Download need something to act on; Upload always does.
  void ScriptMenuPanel::gateActions(bool empty) {
    copyBtn_->setEnabled(!empty);
    downloadBtn_->setEnabled(!empty);
    runBtn_->setEnabled(!empty);
  }

  // The menu stays open on both outcomes: a failed run is exactly when you want the text
  // and the underlines still in front of you.
  void ScriptMenuPanel::run() {
    if (!hooks_.run || edit_->isEmpty()) return;
    hooks_.run(edit_->script());
    showRunDiagnostics();   // from here the strip and the underlines mean this exact text
  }

  void ScriptMenuPanel::restyle(const Palette& pal) {
    const QColor ink = pal.textMain;
    copyBtn_->setIcon(labelIcon(QStringLiteral("clipboard"), ink, MENU_SCRIPT_ICON));
    downloadBtn_->setIcon(labelIcon(QStringLiteral("file-down"), ink, MENU_SCRIPT_ICON));
    uploadBtn_->setIcon(labelIcon(QStringLiteral("file-up"), ink, MENU_SCRIPT_ICON));
    runBtn_->setIcon(labelIcon(QStringLiteral("play"), pal.onAccent, MENU_SCRIPT_ICON));
    edit_->restyleFormats();   // the formats hold resolved colours; the verdict on screen stands
  }

}  // namespace stencil::gui
