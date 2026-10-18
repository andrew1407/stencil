#include "ConsoleWindow.hpp"
#include "consoleGrammar.hpp"

#include <QKeyEvent>
#include <QPointer>
#include <QLineEdit>

// The prompt's keys: Enter runs, Tab cycles the completions, Up/Down walk the history.

namespace stencil::gui {

  void ConsoleWindow::submit(const QString& typed) {
    line.setText(typed);
    const QString text = line.take(support::consoleHistoryText);
    prompt->clear();
    if (text.isEmpty()) return;
    // The echo as the Zig console prints it: the prompt and a known command's word in the accent, a typed key masked.
    const support::ParsedConsoleLine parsed = support::parseConsoleLine(text);
    const QString shown = support::consoleEchoText(text);
    const int wordEnd = shown.indexOf(QLatin1Char(' ')) < 0 ? shown.size() : shown.indexOf(QLatin1Char(' '));
    const int head = support::resolveConsoleCommand(parsed.word, false) ? 2 + wordEnd : 2;
    print({{QStringLiteral("> ") + shown, support::ConsoleKind::ECHO, head}});
    if (!hooks.run) return;
    hooks.run(text, [guard = QPointer<ConsoleWindow>(this)](const support::ConsoleOutput& out) {
      if (!guard) return;
      if (!out.lines.isEmpty()) guard->print(out.lines);
      guard->applyEffect(out.effect);
    });
  }

  bool ConsoleWindow::eventFilter(QObject* o, QEvent* e) {
    if (o != prompt || e->type() != QEvent::KeyPress) return QDialog::eventFilter(o, e);
    auto* k = static_cast<QKeyEvent*>(e);
    switch (k->key()) {
      case Qt::Key_Return:
      case Qt::Key_Enter:
        submit(prompt->text());
        return true;
      case Qt::Key_Tab:
        if (!hooks.completions) return true;
        if (line.text().trimmed() != prompt->text().trimmed()) line.setText(prompt->text().trimmed());
        line.completeWith(hooks.completions, [this](const QStringList& names) {
          QStringList slashed;
          for (const QString& n : names) slashed << QLatin1Char('/') + n;
          print({{slashed.join(QStringLiteral("  ")), support::ConsoleKind::PLAIN}});
        });
        prompt->setText(line.text());
        return true;
      case Qt::Key_Up:
        line.historyUp();
        prompt->setText(line.text());
        return true;
      case Qt::Key_Down:
        line.historyDown();
        prompt->setText(line.text());
        return true;
      default:
        return QDialog::eventFilter(o, e);
    }
  }

}  // namespace stencil::gui
