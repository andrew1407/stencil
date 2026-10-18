// The console grammar and its table (support/console/consoleGrammar): the line forms the Zig console
// takes, the desktop rows of commands.json with their aliases, the CLI-only rows, the
// completions and the help laid out as uiText.txt @help lays it. Browser twin:
// tests/console/shell/{grammar,table,help}.test.js.
#include "consoleGrammar.hpp"

#include <QGuiApplication>
#include <QSet>
#include <cstdio>

#include "../check.hpp"

using namespace stencil::support;
using stencil::support::ConsoleKind;

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);

  std::printf("the line:\n");
  {
    ParsedConsoleLine p = parseConsoleLine(QStringLiteral("/crop x1=10% x2=90%"));
    check(p.slashed && p.word == "crop" && p.arg == "x1=10% x2=90%", "a slashed word and its argument");
    p = parseConsoleLine(QStringLiteral("  Rotate   -1  "));
    check(!p.slashed && p.word == "rotate" && p.arg == "-1", "the slash is optional, the word lower-cased, the arg trimmed");
    p = parseConsoleLine(QStringLiteral("/"));
    check(p.slashed && p.word.isEmpty(), "a bare slash is no word");
    check(splitConsoleWords(QStringLiteral("a, b  c")) == QStringList{"a", "b", "c"}, "words split on spaces and commas");
  }

  std::printf("the table:\n");
  {
    const QVector<CommandSpec>& rows = consoleCommandTable();
    check(rows.size() > 30, "commands.json resolved over the qrc and the desktop rows were read");
    QSet<QString> words;
    bool unique = true;
    for (const CommandSpec& c : rows) {
      for (const QString& w : QStringList{c.name} + c.aliases) {
        unique = unique && !words.contains(w);
        words.insert(w);
      }
      check(consoleSections().contains(c.section), qPrintable(QStringLiteral("row '%1' names a section").arg(c.name)));
    }
    check(unique, "no name or alias is used twice");
    bool browserOnly = false;
    for (const CommandSpec& c : rows) browserOnly = browserOnly || c.name == "open-tab" || c.name == "desktop" || c.name == "browser-tab";
    check(!browserOnly, "the browser's own rows are not here");
    check(resolveConsoleCommand("ROT", false) && resolveConsoleCommand("ROT", false)->name == "rotate", "an alias resolves in any casing");
    check(resolveConsoleCommand("web", false) == nullptr, "a word that is no row resolves to nothing");
    check(resolveConsoleCommand("clioff", false) == nullptr && resolveConsoleCommand("clioff", true), "a CLI row answers only in CLI mode");
    check(resolveConsoleCommand("ls", true) && resolveConsoleCommand("ll", true), "ls and ll are CLI rows");
    check(resolveConsoleCommand("browser", false) && resolveConsoleCommand("open-window", false), "the desktop's hand-off rows are here");
    const QStringList co = consoleCompletions("co", false);
    check(co.contains("color") && co.contains("connect") && co.contains("connections") && !co.contains("clioff"), "completions by prefix, table order, no CLI rows");
    check(consoleCompletions("c", true).contains("clioff"), "…CLI mode completes its own rows too");
    const CliModeConfig& cli = cliModeConfig();
    check(cli.typedWord == "cliclicli" && cli.holdMs == 3000 && cli.offCommand == "clioff", "the CLI mode block is read");
  }

  std::printf("the help:\n");
  {
    const QVector<stencil::support::ConsoleLine> lines = consoleHelpLines(false);
    check(lines.first().text.startsWith("Commands"), "the heading leads");
    int sections = 0, cliRows = 0;
    for (const auto& l : lines) {
      if (l.kind == ConsoleKind::SECTION) ++sections;
      if (l.text.startsWith("/clioff")) ++cliRows;
    }
    check(sections == consoleSections().size() - 1 || sections == consoleSections().size(), "every section with a desktop row is a heading");
    check(cliRows == 0, "the window's help lists no CLI-only row");
    bool gutter = true;
    for (const auto& l : lines)
      if (const int gap = l.text.indexOf("  "); l.text.startsWith('/') && gap >= 0 && gap < HELP_GUTTER)
        gutter = gutter && l.text.at(HELP_GUTTER - 1) == ' ';
    check(gutter, "a row whose head fits the gutter starts its help there");
    const QVector<stencil::support::ConsoleLine> cliLines = consoleHelpLines(true);
    check(cliLines.size() > lines.size() && cliLines.last().text.contains("/clioff"), "CLI mode's help adds its rows and the way out");
  }

  std::printf("a typed key:\n");
  {
    check(consoleTypesKey("/llm key abc123") && consoleTypesKey("llm KEY abc123") && !consoleTypesKey("/llm key") && !consoleTypesKey("/llm model key"),
          "a line types a key when it names llm, key and a value");
    check(consoleEchoText("/llm key abc123") == QString::fromUtf8("/llm key ••••••••") && consoleHistoryText("/llm key abc123") == "/llm key",
          "it is echoed masked and recalled bare (cli llm/key.zig)");
    check(consoleEchoText("/crop x1=1") == "/crop x1=1" && consoleHistoryText("/crop x1=1") == "/crop x1=1", "any other line is itself");
  }

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}
