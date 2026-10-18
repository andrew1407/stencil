#pragma once
// The console's line grammar and its command table, common/config/console/commands.json over the
// qrc: which words name which command on this surface, their help, and the CLI mode's secret
// word. Pure; twin of browser/js/console/shell/{grammar,table,help}.js.
#include "consoleReply.hpp"
#include <QString>
#include <QStringList>
#include <QVector>

namespace stencil::support {

  struct ParsedConsoleLine {
    QString word;    // lower-cased
    QString arg;     // the rest, trimmed
    bool slashed = false;
  };
  // An optional leading '/', the first word, the rest as the argument.
  ParsedConsoleLine parseConsoleLine(const QString& line);
  // Words split on whitespace and commas.
  QStringList splitConsoleWords(const QString& arg);
  // A line that types a key (`/llm key <value>`) is echoed masked and recalled bare (cli llm/key.zig).
  bool consoleTypesKey(const QString& line);
  QString consoleEchoText(const QString& line);
  QString consoleHistoryText(const QString& line);

  struct CommandSpec {
    QString name;
    QStringList aliases;
    QString arg;
    QString help;
    QString section;
    bool cliMode = false;   // answers only while CLI mode is on
  };
  struct CliModeConfig {
    QString typedWord;
    int holdMs = 3000;
    QString offCommand;
  };

  // The desktop's rows, in table order.
  const QVector<CommandSpec>& consoleCommandTable();
  const QStringList& consoleSections();
  const CliModeConfig& cliModeConfig();

  // The row a word names, by name or alias, any casing; null for none (a CLI row only in CLI mode).
  const CommandSpec* resolveConsoleCommand(const QString& word, bool cliMode);
  QStringList consoleCompletions(const QString& prefix, bool cliMode);

  // The /help text as cli/src/console/uiText.txt @help lays it out: a section heading, then
  // `/name arg` in a HELP_GUTTER-wide gutter and the help beside it.
  inline constexpr int HELP_GUTTER = 24;
  QVector<ConsoleLine> consoleHelpLines(bool cliMode);

}  // namespace stencil::support
