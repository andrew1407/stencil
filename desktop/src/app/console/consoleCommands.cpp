#include "consoleCommands.hpp"
#include "ConsoleHost.hpp"
#include "consoleGrammar.hpp"

namespace stencil::gui {

  using support::ConsoleOutput;

  const QHash<QString, ConsoleHandler>& consoleHandlers() {
    static const QHash<QString, ConsoleHandler> table = [] {
      QHash<QString, ConsoleHandler> t;
      registerEditCommands(t);
      registerSessionCommands(t);
      return t;
    }();
    return table;
  }

  void runConsoleLine(ConsoleContext& ctx, const QString& line, support::ConsoleDone done) {
    const support::ParsedConsoleLine parsed = support::parseConsoleLine(line);
    if (parsed.word.isEmpty()) return done(ConsoleOutput());
    const support::CommandSpec* spec = support::resolveConsoleCommand(parsed.word, ctx.cli);
    const ConsoleHandler handler = spec ? consoleHandlers().value(spec->name) : ConsoleHandler();
    if (!handler)
      return done(ConsoleOutput::error(QStringLiteral("unknown command '%1' — type 'help' for the command list").arg(parsed.word)));
    handler(ctx, parsed.arg, std::move(done));
  }

  bool needImage(ConsoleContext& ctx, const support::ConsoleDone& done) {
    if (ctx.host.hasImage()) return true;
    done(ConsoleOutput::error(QStringLiteral("no image loaded — '/upload', '/blank' or '/open' first")));
    return false;
  }

}  // namespace stencil::gui
