#include "ScriptMenuPanel.hpp"

#include "ScriptEditorWidget.hpp"
#include "../support/modalChrome.hpp"   // makeModalCta — Run is the primary action
#include "scriptMenuPanelParts.hpp"

#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace stencil::gui {

  namespace {

    ScriptEditorWidget::Style menuStyle() {
      ScriptEditorWidget::Style s;
      s.glowName = QStringLiteral("scriptMenuGlow");
      s.wrapName = QStringLiteral("scriptMenuWrap");
      s.editName = QStringLiteral("scriptMenuText");
      s.diagName = QStringLiteral("scriptMenuDiag");
      s.fontPx = MENU_SCRIPT_FONT_PX;
      s.padX = MENU_SCRIPT_PAD_X;
      s.padY = MENU_SCRIPT_PAD_Y;
      s.lineHeightPct = MENU_SCRIPT_LINE_HEIGHT_PCT;
      s.editorMinH = MENU_SCRIPT_EDITOR_MIN;
      s.indent = MENU_SCRIPT_INDENT;
      s.codeKeys = true;
      return s;
    }

  }  // namespace

  ScriptMenuPanel::ScriptMenuPanel(QWidget* parent, Hooks hooks)
      : QWidget(parent), hooks_(std::move(hooks)) {
    setObjectName(QStringLiteral("scriptMenuPanel"));
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(12, 2, 12, 6);
    col->setSpacing(0);

    edit_ = new ScriptEditorWidget(this, menuStyle());
    col->addWidget(edit_, 1);   // the editor takes whatever the strip and the row leave

    // Copy · Download · Upload · Run, right-aligned, Run primary and last (browser order).
    // All four are accent-FILLED, like the window's and like a plain <button> in the
    // browser's shell.css; the shared sheet greys the disabled face.
    auto* row = new QHBoxLayout;
    row->setSpacing(6);
    row->addStretch(1);
    const auto mk = [this, row](const char* name, const QString& label, const QString& tip) {
      auto* b = new QPushButton(label, this);
      b->setObjectName(QString::fromLatin1(name));
      b->setToolTip(tip);
      b->setFocusPolicy(Qt::TabFocus);
      makeModalCta(b);
      row->addWidget(b);
      return b;
    };
    copyBtn_ = mk("scriptMenuCopy", tr("Copy"), tr("Copy this script to the clipboard"));
    downloadBtn_ = mk("scriptMenuDownload", tr("Download"), tr("Save this script as a .stc file"));
    uploadBtn_ = mk("scriptMenuUpload", tr("Upload"), tr("Load a .stc file into the editor"));
    runBtn_ = mk("scriptMenuRun", tr("Run"),
                 tr("Run this script on the open project (Ctrl+Enter)"));
    col->addSpacing(8);
    col->addLayout(row);

    connect(copyBtn_, &QPushButton::clicked, this, [this] {
      edit_->copyToClipboard();
      if (hooks_.notice) hooks_.notice(tr("Script copied"));
    });
    connect(downloadBtn_, &QPushButton::clicked, this,
            [this] { if (hooks_.download) hooks_.download(); });
    connect(uploadBtn_, &QPushButton::clicked, this,
            [this] { if (hooks_.upload) hooks_.upload(); });
    connect(runBtn_, &QPushButton::clicked, this, &ScriptMenuPanel::run);
    connect(edit_, &ScriptEditorWidget::runRequested, this, &ScriptMenuPanel::run);
    connect(edit_, &ScriptEditorWidget::edited, this, &ScriptMenuPanel::gateActions);

    setFixedWidth(MENU_SCRIPT_WIDTH);
    setFixedHeight(menuScriptHeight(this));
    gateActions();
  }

  QWidget* ScriptMenuPanel::editor() const { return edit_->editor(); }

  QString ScriptMenuPanel::script() const { return edit_->script(); }

  void ScriptMenuPanel::setScript(const QString& text) { edit_->setScript(text); }

}  // namespace stencil::gui
