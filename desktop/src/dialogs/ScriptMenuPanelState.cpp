#include "ScriptMenuPanel.hpp"

#include "ScriptDoc.hpp"
#include "ScriptHighlighter.hpp"
#include "../support/iconSet.hpp"
#include "../support/theme.hpp"
#include "scriptMenuPanelParts.hpp"

#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QTextBlockFormat>
#include <QTextCursor>

// The script flyout's behaviour: what it reports, what it gates, and the two keys it owns.
namespace stencil::gui {

  void ScriptMenuPanel::repaint(bool withDiagnostics) {
    const model::ScriptDoc program = model::ScriptDoc::parse(edit_->toPlainText());
    painting_ = true;
    highlighter_->setProgram(program, withDiagnostics);
    painting_ = false;

    const model::ScriptDiagnostic* first = nullptr;
    if (withDiagnostics) {
      for (const model::ScriptDiagnostic& d : program.diagnostics()) {
        if (d.error) { first = &d; break; }
        if (!first) first = &d;
      }
    }
    diag_->setText(first ? tr("Line %1:%2 — %3").arg(first->line).arg(first->col).arg(first->message)
                         : QString());
    diag_->setProperty("state", first ? (first->error ? QStringLiteral("error")
                                                      : QStringLiteral("warn"))
                                      : QVariant());
    diag_->style()->unpolish(diag_);
    diag_->style()->polish(diag_);
    gateActions();
  }

  void ScriptMenuPanel::showRunDiagnostics() {
    checked_ = true;
    repaint(true);
  }

  // Run, Copy and Download need something to act on; Upload always does.
  void ScriptMenuPanel::gateActions() {
    const bool empty = edit_->toPlainText().trimmed().isEmpty();
    copyBtn_->setEnabled(!empty);
    downloadBtn_->setEnabled(!empty);
    runBtn_->setEnabled(!empty);
  }

  // The menu stays open on both outcomes: a failed run is exactly when you want the text
  // and the underlines still in front of you.
  void ScriptMenuPanel::run() {
    if (!hooks_.run || edit_->toPlainText().trimmed().isEmpty()) return;
    hooks_.run(edit_->toPlainText());
    showRunDiagnostics();   // from here the strip and the underlines mean this exact text
  }

  void ScriptMenuPanel::copyToClipboard() {
    QApplication::clipboard()->setText(edit_->toPlainText());
    if (hooks_.notice) hooks_.notice(tr("Script copied"));
  }

  void ScriptMenuPanel::restyle(const Palette& pal) {
    const QColor ink = pal.textMain;
    copyBtn_->setIcon(labelIcon(QStringLiteral("clipboard"), ink, MENU_SCRIPT_ICON));
    downloadBtn_->setIcon(labelIcon(QStringLiteral("file-down"), ink, MENU_SCRIPT_ICON));
    uploadBtn_->setIcon(labelIcon(QStringLiteral("file-up"), ink, MENU_SCRIPT_ICON));
    runBtn_->setIcon(labelIcon(QStringLiteral("play"), pal.onAccent, MENU_SCRIPT_ICON));
    highlighter_->restyle();   // the formats hold resolved colours
    repaint(checked_);
  }

  /* Hover lights the halo, focus thickens the border — the browser's .script-editor-wrap
   * :hover / :focus-within. The popup grab means the editor is the only child the menu
   * synthesises an Enter for, so the hover rides on it. */
  bool ScriptMenuPanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched != edit_) return QWidget::eventFilter(watched, event);
    const auto state = [this](const char* key, bool on) {
      for (QFrame* f : {glow_, wrap_}) {
        f->setProperty(key, on);
        f->style()->unpolish(f);
        f->style()->polish(f);
      }
    };
    switch (event->type()) {
      case QEvent::Enter: state("hovered", true); break;
      case QEvent::Leave: state("hovered", false); break;
      case QEvent::FocusIn: state("focused", true); break;
      case QEvent::FocusOut: state("focused", false); break;
      case QEvent::KeyPress: {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Tab && !(ke->modifiers() & Qt::ShiftModifier)) {
          edit_->insertPlainText(QString(MENU_SCRIPT_INDENT, QLatin1Char(' ')));
          return true;
        }
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) &&
            (ke->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))) {
          run();
          return true;
        }
        break;
      }
      default: break;
    }
    return QWidget::eventFilter(watched, event);
  }

  /* Qt has no line-height, so the leading is a block format; Return copies the current
   * block's, so this runs once per document (ScriptDialog.cpp applyLineHeight). */
  void ScriptMenuPanel::applyLineHeight() {
    QTextCursor cur(edit_->document());
    cur.select(QTextCursor::Document);
    QTextBlockFormat fmt;
    fmt.setLineHeight(MENU_SCRIPT_LINE_HEIGHT_PCT, QTextBlockFormat::ProportionalHeight);
    cur.mergeBlockFormat(fmt);
    edit_->document()->clearUndoRedoStacks();
  }

}  // namespace stencil::gui
