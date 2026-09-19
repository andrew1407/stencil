#include "ScriptDialog.hpp"

#include "ScriptEditorWidget.hpp"
#include "../support/modalChrome.hpp"

#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QTextCursor>

namespace stencil::gui {

  namespace {

    // Every metric here is the browser window's (css/components/scriptEditor.css) - one editor on two
    // surfaces. The width is not: it is a share of the action row's fit on this surface's own fonts.
    constexpr int FIT_SHARE_NUM = 15;
    constexpr int FIT_SHARE_DEN = 13;
    constexpr int BAR_PAD_X = 18;   // browser .script-actions-bar padding: 12px 18px
    constexpr int BAR_PAD_Y = 12;
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

    // The actions ride ABOVE the editor (browser .script-actions-bar), so this window builds its own
    // row between header and body: the shell's rule then reads as the bar's bottom border.
    auto* bar = new QHBoxLayout;
    bar->setSpacing(8);
    bar->setContentsMargins(BAR_PAD_X, BAR_PAD_Y, BAR_PAD_X, BAR_PAD_Y);
    chrome.root->insertLayout(2, bar);
    chrome.root->insertWidget(3, modalDivider(chrome.root->parentWidget()));

    editor_ = new ScriptEditorWidget(this, windowStyle());
    chrome.body->addWidget(editor_, 1);   // the editor takes whatever height the window has

    // Run LEADS the row: the primary action is the first one the eye and the cursor reach.
    runBtn_ = new QPushButton(tr("Run"), this);
    runBtn_->setObjectName(QStringLiteral("scriptRun"));
    runBtn_->setToolTip(tr("Run this script on the open project (Ctrl+Enter)"));
    makeModalGo(runBtn_, QStringLiteral("play"));   // the one GO action: green, not accent
    runBtn_->setAutoDefault(false);
    bar->addWidget(runBtn_);

    copyBtn_ = new QPushButton(tr("Copy"), this);
    makeModalCta(copyBtn_, QStringLiteral("clipboard"));
    copyBtn_->setAutoDefault(false);
    bar->addWidget(copyBtn_);

    downloadBtn_ = new QPushButton(tr("Download"), this);
    makeModalCta(downloadBtn_, QStringLiteral("file-down"));
    downloadBtn_->setAutoDefault(false);
    bar->addWidget(downloadBtn_);

    auto* uploadBtn = new QPushButton(tr("Upload"), this);
    makeModalCta(uploadBtn, QStringLiteral("file-up"));
    uploadBtn->setAutoDefault(false);
    bar->addWidget(uploadBtn);

    // At the far end, in the shared danger red: Clear throws work away, so it sits clear of
    // everything the cursor passes on its way to Run.
    clearBtn_ = new QPushButton(tr("Clear"), this);
    clearBtn_->setToolTip(tr("Empty the script editor"));
    makeModalDanger(clearBtn_, QStringLiteral("trash"));
    clearBtn_->setAutoDefault(false);
    bar->addWidget(clearBtn_);
    // The row starts at the LEFT content edge and the slack falls past it
    // (browser .script-actions-bar: justify-content: flex-start).
    bar->addStretch(1);

    connect(copyBtn_, &QPushButton::clicked, editor_, &ScriptEditorWidget::copyToClipboard);
    connect(downloadBtn_, &QPushButton::clicked, this, &ScriptDialog::saveFile);
    connect(uploadBtn, &QPushButton::clicked, this, &ScriptDialog::loadFile);
    connect(clearBtn_, &QPushButton::clicked, this, [this] { editor_->setScript(QString()); });
    connect(runBtn_, &QPushButton::clicked, this, &ScriptDialog::runRequested);
    connect(editor_, &ScriptEditorWidget::runRequested, this, &ScriptDialog::runRequested);
    connect(editor_, &ScriptEditorWidget::edited, this, &ScriptDialog::gateActions);

    if (!initialText.isEmpty()) editor_->setScript(initialText);
    setAcceptDrops(true);
    // Provisional: showEvent re-measures it on the fonts the QSS hands the buttons.
    setFixedWidth(minimumSizeHint().width() * FIT_SHARE_NUM / FIT_SHARE_DEN);
    const int avail = screen() ? screen()->availableGeometry().height() : MODAL_MAX_H;
    resize(width(), qMin(int(avail * MODAL_SCREEN_SHARE), MODAL_MAX_H));
    gateActions();
    editor_->editor()->setFocus();
    editor_->editor()->moveCursor(QTextCursor::End);
  }

  // The window is its widest row, the actions bar; half again that fit leaves the editor room
  // (browser .script-modal). Measured here: the QSS font reaches the buttons post-constructor.
  void ScriptDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    if (isWindow()) setFixedWidth(minimumSizeHint().width() * FIT_SHARE_NUM / FIT_SHARE_DEN);
  }

  QString ScriptDialog::script() const { return editor_->script(); }

  const model::ScriptDoc& ScriptDialog::program() const { return editor_->program(); }

  void ScriptDialog::showRunDiagnostics() { editor_->showRunDiagnostics(); }

  // Copy, Save and Clear need text; Open always has something to do. Run needs something to
  // RUN (browser js/ui/scriptEditor.js gateActions).
  void ScriptDialog::gateActions() {
    const bool blank = editor_->isEmpty();
    copyBtn_->setEnabled(!blank);
    downloadBtn_->setEnabled(!blank);
    clearBtn_->setEnabled(!blank);
    runBtn_->setEnabled(!blank && !editor_->isIdle());
  }

}  // namespace stencil::gui
