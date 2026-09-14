#include "ScriptDialog.hpp"

#include "ScriptEditorWidget.hpp"
#include "../support/modalChrome.hpp"

#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QTextCursor>

namespace stencil::gui {

  namespace {

    // Every metric here is the browser window's (css/components/scriptEditor.css): the two
    // editors are the same window on two surfaces.
    constexpr int MODAL_W = 587;
    constexpr int MODAL_MAX_H = 760;
    constexpr double MODAL_SCREEN_SHARE = 0.82;
    constexpr int EDITOR_MIN_H = 160;
    constexpr int EDITOR_FONT_PX = 13;
    constexpr int LINE_HEIGHT_PCT = 155;   // browser: font 12.5px / line-height 1.55
    constexpr int WRAP_PAD_X = 10;
    constexpr int WRAP_PAD_Y = 8;

    ScriptEditorWidget::Style windowStyle() {
      ScriptEditorWidget::Style s;
      s.glowName = QStringLiteral("scriptEditorGlow");
      s.wrapName = QStringLiteral("scriptEditorWrap");
      s.editName = QStringLiteral("scriptText");
      s.diagName = QStringLiteral("scriptDiag");
      s.fontPx = EDITOR_FONT_PX;
      s.padX = WRAP_PAD_X;
      s.padY = WRAP_PAD_Y;
      s.lineHeightPct = LINE_HEIGHT_PCT;
      s.editorMinH = EDITOR_MIN_H;
      s.stripGap = BODY_SPACING;    // the gap the modal body left between the two
      s.hoverOnFrame = true;
      s.tabStops = true;
      return s;
    }

  }  // namespace

  ScriptDialog::ScriptDialog(const QString& initialText, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("stencilScriptDialog"));
    setWindowTitle(tr("Stencil Script"));
    ModalChrome chrome = installModalChrome(this, QStringLiteral("script"), tr("Stencil Script"));

    editor_ = new ScriptEditorWidget(this, windowStyle());
    chrome.body->addWidget(editor_, 1);   // the editor takes whatever height the window has

    QHBoxLayout* footer = addModalFooter(chrome, tr("Write a .stc script and run it here."));
    copyBtn_ = new QPushButton(tr("Copy"), this);
    makeModalCta(copyBtn_, QStringLiteral("clipboard"));
    copyBtn_->setAutoDefault(false);
    footer->addWidget(copyBtn_);

    downloadBtn_ = new QPushButton(tr("Download"), this);
    makeModalCta(downloadBtn_, QStringLiteral("file-down"));
    downloadBtn_->setAutoDefault(false);
    footer->addWidget(downloadBtn_);

    auto* uploadBtn = new QPushButton(tr("Upload"), this);
    makeModalCta(uploadBtn, QStringLiteral("file-up"));
    uploadBtn->setAutoDefault(false);
    footer->addWidget(uploadBtn);

    runBtn_ = new QPushButton(tr("Run"), this);
    runBtn_->setObjectName(QStringLiteral("scriptRun"));
    runBtn_->setToolTip(tr("Run this script on the open project (Ctrl+Enter)"));
    makeModalCta(runBtn_, QStringLiteral("play"));
    runBtn_->setAutoDefault(false);
    footer->addWidget(runBtn_);

    connect(copyBtn_, &QPushButton::clicked, editor_, &ScriptEditorWidget::copyToClipboard);
    connect(downloadBtn_, &QPushButton::clicked, this, &ScriptDialog::saveFile);
    connect(uploadBtn, &QPushButton::clicked, this, &ScriptDialog::loadFile);
    connect(runBtn_, &QPushButton::clicked, this, &QDialog::accept);
    connect(editor_, &ScriptEditorWidget::edited, this, &ScriptDialog::gateActions);

    if (!initialText.isEmpty()) editor_->setScript(initialText);
    setAcceptDrops(true);
    setFixedWidth(MODAL_W);
    const int avail = screen() ? screen()->availableGeometry().height() : MODAL_MAX_H;
    resize(MODAL_W, qMin(int(avail * MODAL_SCREEN_SHARE), MODAL_MAX_H));
    gateActions();
    editor_->editor()->setFocus();
    editor_->editor()->moveCursor(QTextCursor::End);
  }

  QString ScriptDialog::script() const { return editor_->script(); }

  const model::ScriptDoc& ScriptDialog::program() const { return editor_->program(); }

  void ScriptDialog::showRunDiagnostics() { editor_->showRunDiagnostics(); }

  // Copy and Save need text; Open always has something to do. Run needs something to RUN
  // (browser js/ui/scriptEditor.js gateActions).
  void ScriptDialog::gateActions() {
    const bool blank = editor_->isEmpty();
    copyBtn_->setEnabled(!blank);
    downloadBtn_->setEnabled(!blank);
    runBtn_->setEnabled(!blank && !editor_->isIdle());
  }

}  // namespace stencil::gui
