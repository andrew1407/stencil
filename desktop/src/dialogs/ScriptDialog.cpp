#include "ScriptDialog.hpp"

#include "ScriptHighlighter.hpp"
#include "ScriptDoc.hpp"
#include "../support/modalChrome.hpp"

#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QStyle>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextStream>

namespace stencil::gui {

  namespace {

    constexpr int EDITOR_ROWS = 22;   // the browser window's height, in lines

    const QString& scriptFilter() {
      static const QString filter = QStringLiteral("Stencil script (*.stc)");
      return filter;
    }

  }  // namespace

  ScriptDialog::ScriptDialog(const QString& initialText, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("stencilScriptDialog"));
    setWindowTitle(tr("Stencil Script"));
    ModalChrome chrome = installModalChrome(this, QStringLiteral("script"), tr("Stencil Script"));

    edit_ = new QPlainTextEdit(initialText, this);
    edit_->setObjectName(QStringLiteral("scriptText"));
    edit_->setPlaceholderText(QStringLiteral("@crop 10%\n@filter bw\n@save"));
    edit_->setLineWrapMode(QPlainTextEdit::NoWrap);   // a script is code; let it scroll
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(mono.pointSizeF() > 0 ? mono.pointSizeF() : 12.0);
    edit_->setFont(mono);
    edit_->setTabStopDistance(2 * edit_->fontMetrics().horizontalAdvance(QLatin1Char(' ')));
    edit_->setFixedHeight(edit_->fontMetrics().lineSpacing() * EDITOR_ROWS + 16);
    chrome.body->addWidget(edit_);

    diag_ = new QLabel(this);
    diag_->setObjectName(QStringLiteral("scriptDiag"));
    diag_->setWordWrap(true);
    chrome.body->addWidget(diag_);
    chrome.body->addStretch(1);

    highlighter_ = new dialogs::ScriptHighlighter(edit_->document());

    QHBoxLayout* footer = addModalFooter(chrome, tr("Write a .stc script and run it here."));
    copyBtn_ = new QPushButton(tr("Copy"), this);
    makeModalCta(copyBtn_, QStringLiteral("clipboard"));
    copyBtn_->setAutoDefault(false);
    footer->addWidget(copyBtn_);

    downloadBtn_ = new QPushButton(tr("Save…"), this);
    makeModalCta(downloadBtn_, QStringLiteral("file-down"));
    downloadBtn_->setAutoDefault(false);
    footer->addWidget(downloadBtn_);

    auto* uploadBtn = new QPushButton(tr("Open…"), this);
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
      checked_ = false;   // editing clears the last verdict: it was about older text
      repaint(false);
    });

    setFixedWidth(MODAL_WIDTH);
    adjustSize();
    repaint(false);
    edit_->setFocus();
    edit_->moveCursor(QTextCursor::End);
  }

  QString ScriptDialog::script() const { return edit_->toPlainText(); }

  void ScriptDialog::repaint(bool withDiagnostics) {
    const model::ScriptDoc program = model::ScriptDoc::parse(edit_->toPlainText());
    highlighter_->setProgram(program, withDiagnostics);

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

  void ScriptDialog::loadFile() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Open script"), QString(),
                                                      scriptFilter());
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    edit_->setPlainText(QString::fromUtf8(file.readAll()));
  }

  void ScriptDialog::saveFile() {
    const QString path = QFileDialog::getSaveFileName(this, tr("Save script"),
                                                      QStringLiteral("stencil.stc"),
                                                      scriptFilter());
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    file.write(edit_->toPlainText().toUtf8());
  }

}  // namespace stencil::gui
