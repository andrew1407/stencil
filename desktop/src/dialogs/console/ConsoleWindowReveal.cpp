#include "ConsoleWindow.hpp"
#include "../../support/console/TypewriterReveal.hpp"

#include <QPlainTextEdit>
#include <QTextCursor>

// The scrollback as it types in: the settled lines stay written, the unsettled tail is rewritten
// on every tick with each line's visible cut, coloured by its kind.

namespace stencil::gui {

  void ConsoleWindow::resetDocument() {
    output->clear();
    committed = 0;
    tailStart = 0;
  }

  void ConsoleWindow::repaintAll() {
    retintModalChrome(chrome, this);
    resetDocument();
    repaintTail();
  }

  void ConsoleWindow::repaintTail() {
    QTextCursor c(output->document());
    c.beginEditBlock();
    c.setPosition(tailStart);
    c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    c.removeSelectedText();
    int settledEnd = tailStart;
    bool stillSettled = true;
    for (int i = committed; i < reveal->count(); ++i) {
      const support::TypewriterReveal::Line& line = reveal->at(i);
      if (i > 0) c.insertText(QStringLiteral("\n"));
      QTextCharFormat fmt;
      const bool apart = line.head > 0 && (line.kind == support::ConsoleKind::ECHO || line.kind == support::ConsoleKind::ERROR);
      fmt.setForeground(apart ? inkFor(support::ConsoleKind::PLAIN) : inkFor(line.kind));
      if (line.kind == support::ConsoleKind::TITLE) fmt.setFontWeight(QFont::Bold);
      if (line.kind == support::ConsoleKind::TITLE) {
        fmt.setBackground(inkFor(line.kind));
        fmt.setForeground(palette().color(QPalette::Base));
      }
      const QString shown = reveal->visible(i);
      const int head = qMin(line.head, int(shown.size()));
      if (head > 0) {   // a help row's command or an echo's word in the accent; `error: ` bold red
        QTextCharFormat hf = fmt;
        hf.setForeground(inkFor(line.kind == support::ConsoleKind::ERROR ? support::ConsoleKind::ERROR : support::ConsoleKind::ECHO));
        if (line.kind == support::ConsoleKind::ERROR) hf.setFontWeight(QFont::Bold);
        c.insertText(shown.left(head), hf);
      }
      c.insertText(shown.mid(head), fmt);
      if (stillSettled && line.cut >= line.text.size()) {
        settledEnd = c.position();
        ++committed;
      } else {
        stillSettled = false;
      }
    }
    c.endEditBlock();
    tailStart = settledEnd;
    // The scrollbar's range lags the document layout; the cursor at the end is what scrolls reliably.
    QTextCursor end(output->document());
    end.movePosition(QTextCursor::End);
    output->setTextCursor(end);
    output->ensureCursorVisible();
  }

}  // namespace stencil::gui
