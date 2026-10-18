#pragma once
// What one console command answers: lines of a kind the view colours, and an effect the view
// performs. Shared by the console window, the CLI stage and the handlers; twin of the kinds
// browser/js/ui/console/view.js paints.
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

namespace stencil::support {

  enum class ConsoleKind { PLAIN, ECHO, ERROR, SECTION, SECRET, TITLE, MUTED };

  struct ConsoleLine {
    QString text;
    ConsoleKind kind = ConsoleKind::PLAIN;
    int head = 0;   // leading characters in the accent: a help row's command
  };

  enum class ConsoleEffect { NONE, CLEAR, EXIT };

  struct ConsoleOutput {
    QVector<ConsoleLine> lines;
    ConsoleEffect effect = ConsoleEffect::NONE;

    static ConsoleOutput plain(const QStringList& texts) {
      ConsoleOutput out;
      for (const QString& t : texts) out.lines.push_back({t, ConsoleKind::PLAIN});
      return out;
    }
    static ConsoleOutput plain(const QString& text) { return plain(QStringList{text}); }
    static ConsoleOutput error(const QString& text) {
      ConsoleOutput out;
      out.lines.push_back({QStringLiteral("error: ") + text, ConsoleKind::ERROR, 7});   // `error: ` bold red, the message plain
      return out;
    }
    static ConsoleOutput withEffect(ConsoleEffect e) {
      ConsoleOutput out;
      out.effect = e;
      return out;
    }
  };

  using ConsoleDone = std::function<void(const ConsoleOutput&)>;

}  // namespace stencil::support
