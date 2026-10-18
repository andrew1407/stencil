#pragma once
// The console's command handlers, one per commands.json row the desktop carries, keyed by the
// row's name: each parses its argument and asks ConsoleHost for the flow, answering `done`
// with the lines to print. Twin of browser/js/console/shell/handlers*.js.
#include "consoleReply.hpp"
#include <QHash>
#include <QString>
#include <functional>

namespace stencil::gui {

  class ConsoleHost;

  struct ConsoleContext {
    ConsoleHost& host;
    bool cli = false;   // the CLI stage, not the window: the CLI-only rows answer
  };

  using ConsoleHandler = std::function<void(ConsoleContext&, const QString& arg, support::ConsoleDone done)>;

  const QHash<QString, ConsoleHandler>& consoleHandlers();
  // One typed line: the table lookup, then its handler; an unknown word is an error line.
  void runConsoleLine(ConsoleContext& ctx, const QString& line, support::ConsoleDone done);

  // The two halves of the table (consoleCommandsEdit.cpp, consoleCommandsNet.cpp).
  void registerEditCommands(QHash<QString, ConsoleHandler>& table);
  void registerSessionCommands(QHash<QString, ConsoleHandler>& table);

  // "no image loaded — …" when the flow needs a picture and there is none.
  bool needImage(ConsoleContext& ctx, const support::ConsoleDone& done);

}  // namespace stencil::gui
