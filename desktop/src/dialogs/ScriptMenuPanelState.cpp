#include "ScriptMenuPanel.hpp"

#include "ScriptEditorWidget.hpp"
#include "../support/iconSet.hpp"
#include "../support/theme.hpp"
#include "scriptMenuPanelParts.hpp"

#include <QHBoxLayout>
#include <QPushButton>

// The script flyout's behaviour: what it reports, what it gates, and how a theme flip re-inks it.
namespace stencil::gui {

  void ScriptMenuPanel::showRunDiagnostics() { edit_->showRunDiagnostics(); }

  // Copy, Download and Clear need text; Upload always has something to do. Run needs
  // something to RUN (browser js/ui/scriptEditor.js gateActions).
  void ScriptMenuPanel::gateActions() {
    const bool blank = edit_->isEmpty();
    copyBtn_->setEnabled(!blank);
    downloadBtn_->setEnabled(!blank);
    clearBtn_->setEnabled(!blank);
    runBtn_->setEnabled(!blank && !edit_->isIdle());
  }

  // The menu stays open on both outcomes: a failed run is exactly when you want the text
  // and the underlines still in front of you.
  void ScriptMenuPanel::run() {
    if (!hooks_.run || !runBtn_->isEnabled()) return;   // Ctrl+Enter obeys the button's gate
    hooks_.run(edit_->script());
    showRunDiagnostics();   // from here the strip and the underlines mean this exact text
  }

  void ScriptMenuPanel::restyle(const Palette& pal) {
    const QColor ink = pal.textMain;
    copyBtn_->setIcon(labelIcon(QStringLiteral("clipboard"), ink, MENU_SCRIPT_ICON));
    downloadBtn_->setIcon(labelIcon(QStringLiteral("file-down"), ink, MENU_SCRIPT_ICON));
    uploadBtn_->setIcon(labelIcon(QStringLiteral("file-up"), ink, MENU_SCRIPT_ICON));
    // White on the danger fill, like every other #dangerButton glyph (trash = delete).
    clearBtn_->setIcon(labelIcon(QStringLiteral("trash"), QColor(Qt::white), MENU_SCRIPT_ICON));
    runBtn_->setIcon(labelIcon(QStringLiteral("play"), pal.onAccent, MENU_SCRIPT_ICON));
    edit_->restyleFormats();   // the formats hold resolved colours; the verdict on screen stands
    // The glyphs just set change what the row needs, and the MENU sizes this panel while it is
    // still hidden — so the width is re-derived here, never from a stale hint on the way in.
    setFixedWidth(rowWidth());
  }

}  // namespace stencil::gui
