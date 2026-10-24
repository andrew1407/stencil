#include "ConsoleHost.hpp"
#include "MainWindow.hpp"
#include "../open/launchOptions.hpp"

#include <QUrl>

// A launch link at the console's /open (contracts/launch/launch-uri.md): the desktop's subset of
// it, applied as argv would. Browser twin: applyLaunchQuery in js/console/launchQuery.js.

namespace stencil::gui {

  using support::ConsoleOutput;

  bool ConsoleHost::openLaunchLink(const QString& ref, const support::ConsoleDone& done) {
    const auto link = launchUri::parse(QUrl(ref));
    if (!link || !link->actionable()) return false;
    LaunchOptions o;
    adoptLaunchUri(o, *link);
    if (o.empty()) {
      done(ConsoleOutput::error(QStringLiteral("the link names nothing this app opens — a blank, the name, the editor settings, the assistant and the chat have no desktop form")));
      return true;
    }
    w.applyLaunchOptions(o);
    QStringList said{QStringLiteral("opened the launch link")};
    for (const launchUri::Warning& x : link->warnings) {
      said << (x.code == launchUri::WarningCode::SECRET_IN_QUERY
                   ? QStringLiteral("\"%1\" is ignored in the ?query — put it in the #fragment").arg(x.key)
                   : QStringLiteral("\"%1\" was left out").arg(x.key));
    }
    done(ConsoleOutput::plain(said.join(QStringLiteral("; "))));
    return true;
  }

}  // namespace stencil::gui
