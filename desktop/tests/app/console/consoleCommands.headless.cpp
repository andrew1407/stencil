// Table ↔ handler parity for the console (app/console/consoleCommands): every desktop row of
// commands.json has a handler and every handler a row; a line runs through the table to its
// handler, an unknown word is one error line, and the status and help answer without a picture.
// Browser twin: tests/config/consoleCommands.test.js, tests/console/shell/interpreter.test.js.
#include "consoleCommands.hpp"
#include "consoleGrammar.hpp"
#include "ConsoleHost.hpp"
#include "MainWindow.hpp"
#include "SessionKey.hpp"

#include <QApplication>
#include <QSet>
#include <cstdio>

#include "../../support/check.hpp"

using namespace stencil::gui;
using namespace stencil::support;
using stencil::support::ConsoleEffect;
using stencil::support::ConsoleKind;
using stencil::support::ConsoleOutput;

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  std::printf("the handlers and the rows:\n");
  const QHash<QString, ConsoleHandler>& handlers = consoleHandlers();
  QSet<QString> rows;
  for (const CommandSpec& c : consoleCommandTable()) {
    rows.insert(c.name);
    check(handlers.contains(c.name), qPrintable(QStringLiteral("row '%1' has a handler").arg(c.name)));
  }
  for (auto it = handlers.begin(); it != handlers.end(); ++it)
    check(rows.contains(it.key()), qPrintable(QStringLiteral("handler '%1' has a row").arg(it.key())));

  std::printf("a line against a window:\n");
  {
    MainWindow win(nullptr, /*restoreLast=*/false);
    ConsoleHost host(win);
    ConsoleContext window{host, false};
    ConsoleContext cli{host, true};
    ConsoleOutput got;
    const auto take = [&got](const ConsoleOutput& out) { got = out; };
    runConsoleLine(window, QStringLiteral("/nope now"), take);
    check(got.lines.size() == 1 && got.lines.first().kind == ConsoleKind::ERROR && got.lines.first().text == "error: unknown command 'nope' — type 'help' for the command list",
          "an unknown word is one error line naming it");
    runConsoleLine(window, QStringLiteral("clioff"), take);
    check(got.lines.size() == 1 && got.lines.first().kind == ConsoleKind::ERROR, "a CLI-only word is unknown to the window");
    runConsoleLine(cli, QStringLiteral("clioff"), take);
    check(got.lines.isEmpty() && got.effect == ConsoleEffect::EXIT, "…and leaves in CLI mode");
    runConsoleLine(window, QStringLiteral("/exit"), take);
    check(got.effect == ConsoleEffect::EXIT, "/exit is the way out of either");
    runConsoleLine(window, QStringLiteral("cls"), take);
    check(got.effect == ConsoleEffect::CLEAR, "an alias reaches its handler");
    runConsoleLine(window, QStringLiteral("status"), take);
    check(got.lines.size() == 1 && got.lines.first().text.startsWith("image: (none)"), "the status line without a picture");
    runConsoleLine(window, QStringLiteral("/crop x1=10%"), take);
    check(got.lines.size() == 1 && got.lines.first().text.startsWith("error: no image loaded"), "an edit without a picture says so");
    runConsoleLine(window, QStringLiteral("/rotate x"), take);
    check(got.lines.size() == 1 && got.lines.first().text.startsWith("error: no image loaded"), "…before it reads its argument");
    runConsoleLine(window, QStringLiteral("/help"), take);
    bool heading = false, cliRow = false;
    for (const auto& l : got.lines) {
      heading = heading || l.text.startsWith("Commands");
      cliRow = cliRow || l.text.startsWith("/clioff");
    }
    check(heading && !cliRow && got.lines.size() > 30, "the window's help lists the table without the CLI rows");
    runConsoleLine(cli, QStringLiteral("/help"), take);
    cliRow = false;
    for (const auto& l : got.lines) cliRow = cliRow || l.text.startsWith("/clioff");
    check(cliRow, "CLI mode's help lists its own rows");
    runConsoleLine(window, QStringLiteral("/theme"), take);
    check(got.lines.size() == 2 && got.lines.first().text.startsWith("theme: ") && got.lines.at(1).text.contains("violet"), "/theme bare reports the mode and the accents");
    runConsoleLine(window, QStringLiteral("/theme nope"), take);
    check(got.lines.size() == 1 && got.lines.first().kind == ConsoleKind::ERROR && got.lines.first().text.contains("violet"), "an unknown theme word lists what there is");
    runConsoleLine(window, QStringLiteral("/sync maybe"), take);
    check(got.lines.first().kind == ConsoleKind::ERROR, "sync takes on or off");
    runConsoleLine(cli, QStringLiteral("ls"), take);
    check(got.lines.size() == 1 && got.lines.first().text == "(no projects)", "ls with no saved project");
    runConsoleLine(window, QStringLiteral("/connections"), take);
    check(got.lines.size() == 1 && got.lines.first().text == "(no server connections)", "connections with none");
    runConsoleLine(window, QStringLiteral("/connect"), take);
    check(got.lines.first().kind == ConsoleKind::ERROR, "connect needs a URL");
    runConsoleLine(window, QStringLiteral("/llm"), take);
    check(got.lines.size() == 3 && got.lines.first().text.startsWith("provider: "), "/llm bare reports the provider, url and model");
    runConsoleLine(window, QStringLiteral("/llm nope x"), take);
    check(got.lines.first().kind == ConsoleKind::ERROR, "an unknown llm key is an error");
    {
      runConsoleLine(window, QStringLiteral("/llm provider anthropic"), take);
      runConsoleLine(window, QStringLiteral("/llm key abc123"), take);
      check(stencil::llm::SessionKey::instance().key() == "abc123" && got.lines.first().text == "key: (set, hidden)",
            "an Anthropic key is held for the session and reported hidden");
      runConsoleLine(window, QStringLiteral("/llm provider openai"), take);
      runConsoleLine(window, QStringLiteral("/llm key"), take);
      check(got.lines.first().text == "key: (unset)", "...and never reached the stored setting");
      runConsoleLine(window, QStringLiteral("/llm provider anthropic"), take);
      runConsoleLine(window, QStringLiteral("/llm key forget"), take);
      check(stencil::llm::SessionKey::instance().key().isEmpty() && got.lines.first().text == "key: (unset)", "forget drops it");
      runConsoleLine(window, QStringLiteral("/llm provider openai"), take);
      runConsoleLine(window, QStringLiteral("/llm key abc123"), take);
      runConsoleLine(window, QStringLiteral("/llm key"), take);
      check(got.lines.first().text == "key: (set)", "an openai-compatible key is a stored setting");
      runConsoleLine(window, QStringLiteral("/llm key forget"), take);
      runConsoleLine(window, QStringLiteral("/llm key"), take);
      check(got.lines.first().text == "key: (unset)", "and forget clears it");
      runConsoleLine(window, QStringLiteral("/llm provider none"), take);
    }
    runConsoleLine(window, QStringLiteral("/browser maybe"), take);
    check(got.lines.first().kind == ConsoleKind::ERROR, "browser takes [incognito] only");
    check(!host.cliModeOn() && !host.cliGate("x").has_value(), "no stage, no gate");
  }

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}
