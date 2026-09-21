#include "ScriptMenuPanel.hpp"

#include "ScriptEditorWidget.hpp"
#include "../../support/icon/iconSet.hpp"
#include "../../support/theme/theme.hpp"
#include "scriptMenuPanelParts.hpp"

#include <QHBoxLayout>
#include <QPushButton>

// The script flyout's behaviour: what it reports, what it gates, and how a theme flip re-inks it.
namespace stencil::gui {

  void ScriptMenuPanel::showRunDiagnostics() { edit->showRunDiagnostics(); }

  // Copy, Download and Clear need text; Upload always has something to do. Run needs
  // something to RUN (browser js/ui/editor.js gateActions).
  void ScriptMenuPanel::gateActions() {
    const bool blank = edit->isEmpty();
    copyBtn->setEnabled(!blank);
    downloadBtn->setEnabled(!blank);
    clearBtn->setEnabled(!blank);
    runBtn->setEnabled(!blank && !edit->isIdle());
  }

  // The menu stays open on both outcomes: a failed run is exactly when you want the text
  // and the underlines still in front of you.
  void ScriptMenuPanel::run() {
    if (!hooks.run || !runBtn->isEnabled()) return;   // Ctrl+Enter obeys the button's gate
    hooks.run(edit->script());
    showRunDiagnostics();   // from here the strip and the underlines mean this exact text
  }

  void ScriptMenuPanel::restyle(const Palette& pal) {
    const QColor ink = pal.textMain;
    copyBtn->setIcon(labelIcon(QStringLiteral("clipboard"), ink, MENU_SCRIPT_ICON));
    downloadBtn->setIcon(labelIcon(QStringLiteral("file-down"), ink, MENU_SCRIPT_ICON));
    uploadBtn->setIcon(labelIcon(QStringLiteral("file-up"), ink, MENU_SCRIPT_ICON));
    // White on the danger fill, like every other #dangerButton glyph (trash = delete).
    clearBtn->setIcon(labelIcon(QStringLiteral("trash"), QColor(Qt::white), MENU_SCRIPT_ICON));
    runBtn->setIcon(labelIcon(QStringLiteral("play"), pal.onAccent, MENU_SCRIPT_ICON));
    edit->restyleFormats();   // the formats hold resolved colours; the verdict on screen stands
    // The glyphs just set change what the row needs, and the MENU sizes this panel while it is
    // still hidden — so the width is re-derived here, never from a stale hint on the way in.
    setFixedWidth(rowWidth());
  }

}  // namespace stencil::gui
