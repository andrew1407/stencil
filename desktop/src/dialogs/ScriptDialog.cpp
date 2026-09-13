#include "ScriptDialog.hpp"

#include "ScriptHighlighter.hpp"
#include "ScriptDoc.hpp"
#include "../support/modalChrome.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QScreen>
#include <QVBoxLayout>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QStyle>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextStream>
#include <QUrl>

namespace stencil::gui {

  namespace {

    // Every metric here is the browser window's (css/components/scriptEditor.css): the two
    // editors are the same window on two surfaces.
    constexpr int MODAL_W = 587;
    constexpr int MODAL_MAX_H = 760;
    constexpr double MODAL_SCREEN_SHARE = 0.82;
    constexpr int EDITOR_MIN_H = 160;
    constexpr int EDITOR_FONT_PX = 13;
    constexpr int WRAP_PAD_X = 10;
    constexpr int WRAP_PAD_Y = 8;

  }  // namespace

  ScriptDialog::ScriptDialog(const QString& initialText, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("stencilScriptDialog"));
    setWindowTitle(tr("Stencil Script"));
    ModalChrome chrome = installModalChrome(this, QStringLiteral("script"), tr("Stencil Script"));

    /* The browser's .script-editor-wrap: the frame owns the border, the gutter and the
     * background, and the editor inside it is bare. The frame is also what the hover glow
     * hangs on — a graphics effect on the editor itself smears its scrolling viewport. */
    wrap_ = new QFrame(this);
    wrap_->setObjectName(QStringLiteral("scriptEditorWrap"));
    auto* wrapLayout = new QVBoxLayout(wrap_);
    wrapLayout->setContentsMargins(WRAP_PAD_X, WRAP_PAD_Y, WRAP_PAD_X, WRAP_PAD_Y);
    wrap_->setAttribute(Qt::WA_Hover, true);
    wrap_->installEventFilter(this);

    edit_ = new QPlainTextEdit(initialText, wrap_);
    edit_->setObjectName(QStringLiteral("scriptText"));
    edit_->setPlaceholderText(QStringLiteral("@crop 10%\n@filter bw\n@save"));
    edit_->setLineWrapMode(QPlainTextEdit::NoWrap);   // a script is code; let it scroll
    edit_->setFrameShape(QFrame::NoFrame);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPixelSize(EDITOR_FONT_PX);
    edit_->setFont(mono);
    edit_->setTabStopDistance(2 * edit_->fontMetrics().horizontalAdvance(QLatin1Char(' ')));
    edit_->installEventFilter(this);
    wrapLayout->addWidget(edit_);
    wrap_->setMinimumHeight(EDITOR_MIN_H);
    chrome.body->addWidget(wrap_, 1);   // the editor takes whatever height the window has

    diag_ = new QLabel(this);
    diag_->setObjectName(QStringLiteral("scriptDiag"));
    diag_->setWordWrap(true);
    chrome.body->addWidget(diag_);

    highlighter_ = new dialogs::ScriptHighlighter(edit_->document());

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

    connect(copyBtn_, &QPushButton::clicked, this, &ScriptDialog::copyToClipboard);
    connect(downloadBtn_, &QPushButton::clicked, this, &ScriptDialog::saveFile);
    connect(uploadBtn, &QPushButton::clicked, this, &ScriptDialog::loadFile);
    connect(runBtn_, &QPushButton::clicked, this, &QDialog::accept);
    // Painting is a lex of one screenful, so it runs on the keystroke: the colours ARE the
    // text as far as the reader is concerned, and a deferred paint reads as lag.
    connect(edit_, &QPlainTextEdit::textChanged, this, [this] {
      if (painting_) return;
      checked_ = false;   // editing clears the last verdict: it was about older text
      repaint(false);
    });

    setAcceptDrops(true);
    setFixedWidth(MODAL_W);
    const int avail = screen() ? screen()->availableGeometry().height() : MODAL_MAX_H;
    resize(MODAL_W, qMin(int(avail * MODAL_SCREEN_SHARE), MODAL_MAX_H));
    repaint(false);
    edit_->setFocus();
    edit_->moveCursor(QTextCursor::End);
  }

  QString ScriptDialog::script() const { return edit_->toPlainText(); }

  void ScriptDialog::repaint(bool withDiagnostics) {
    const model::ScriptDoc program = model::ScriptDoc::parse(edit_->toPlainText());
    painting_ = true;
    highlighter_->setProgram(program, withDiagnostics);
    painting_ = false;

    if (!withDiagnostics) {
      diag_->clear();
      diag_->setProperty("state", QVariant());
    } else {
      const model::ScriptDiagnostic* first = nullptr;
      for (const model::ScriptDiagnostic& d : program.diagnostics()) {
        if (d.error) { first = &d; break; }
        if (!first) first = &d;
      }
      diag_->setText(first ? tr("Line %1:%2 — %3").arg(first->line).arg(first->col).arg(first->message)
                           : QString());
      diag_->setProperty("state", first ? (first->error ? QStringLiteral("error")
                                                        : QStringLiteral("warn"))
                                        : QVariant());
    }
    diag_->style()->unpolish(diag_);
    diag_->style()->polish(diag_);
    gateActions();
  }

  void ScriptDialog::showRunDiagnostics() {
    checked_ = true;
    repaint(true);
  }

  // Copy, Save and Run need something to act on; Open always does.
  void ScriptDialog::gateActions() {
    const bool empty = edit_->toPlainText().trimmed().isEmpty();
    copyBtn_->setEnabled(!empty);
    downloadBtn_->setEnabled(!empty);
    runBtn_->setEnabled(!empty);
  }

  void ScriptDialog::copyToClipboard() {
    QApplication::clipboard()->setText(edit_->toPlainText());
  }

  /* Hover lights a small glow in the accent, focus thickens the border — the browser's
   * .script-editor-wrap :hover / :focus-within, which QSS has no box-shadow for. */
  bool ScriptDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched == wrap_) {
      if (event->type() == QEvent::Enter) setWrapState("hovered", true);
      else if (event->type() == QEvent::Leave) setWrapState("hovered", false);
    } else if (watched == edit_) {
      if (event->type() == QEvent::FocusIn) setWrapState("focused", true);
      else if (event->type() == QEvent::FocusOut) setWrapState("focused", false);
    }
    return QDialog::eventFilter(watched, event);
  }

  void ScriptDialog::setWrapState(const char* key, bool on) {
    if (!wrap_) return;
    wrap_->setProperty(key, on);
    wrap_->style()->unpolish(wrap_);
    wrap_->style()->polish(wrap_);
    const bool glow = wrap_->property("hovered").toBool();
    if (!glow) { wrap_->setGraphicsEffect(nullptr); return; }
    if (wrap_->graphicsEffect()) return;
    auto* fx = new QGraphicsDropShadowEffect(wrap_);
    fx->setBlurRadius(14);
    fx->setOffset(0, 0);
    QColor tint = palette().color(QPalette::Highlight);
    tint.setAlpha(140);
    fx->setColor(tint);
    wrap_->setGraphicsEffect(fx);
  }

}  // namespace stencil::gui
