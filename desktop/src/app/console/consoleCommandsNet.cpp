// The session half of the console's commands: servers and projects, the assistant, the theme,
// the browser hand-off and the console's own verbs, the CLI-only listings among them.
#include "consoleCommands.hpp"
#include "ConsoleHost.hpp"
#include "consoleGrammar.hpp"

namespace stencil::gui {

  using support::ConsoleEffect;
  using support::ConsoleOutput;

  namespace {
    // A `/connect` word that reads as a server rather than a token: a scheme, a dot or a port, or localhost.
    bool looksLikeServerUrl(const QString& word) {
      return word.contains(QLatin1String("://")) || word.contains(QLatin1Char('.')) ||
             word.contains(QLatin1Char(':')) || word.toLower() == QLatin1String("localhost");
    }
    struct ConnectTarget {
      QString url, token;
      bool tokenSet = false;
    };
    // Each URL starts a connection and one following token word attaches to it.
    QVector<ConnectTarget> parseConnectArgs(const QString& arg) {
      QVector<ConnectTarget> out;
      for (const QString& word : support::splitConsoleWords(arg)) {
        if (!out.isEmpty() && !out.last().tokenSet && !looksLikeServerUrl(word)) {
          out.last().token = word;
          out.last().tokenSet = true;
        } else {
          out.push_back({word, QString(), false});
        }
      }
      return out;
    }
    // Connects the targets one after another, then prints the connections. The host outlives the
    // replies; the context that asked does not, so only the host travels.
    void connectEach(ConsoleHost& host, QVector<ConnectTarget> targets, int i, support::ConsoleDone done) {
      if (i >= targets.size()) return done(ConsoleOutput::plain(host.connectionsRows()));
      host.connectThen(targets[i].url, targets[i].token, [&host, targets, i, done](const QString& err) {
        if (!err.isEmpty()) return done(ConsoleOutput::error(err));
        connectEach(host, targets, i + 1, done);
      });
    }
  }  // namespace

  void registerSessionCommands(QHash<QString, ConsoleHandler>& t) {
    t.insert(QStringLiteral("connect"), [](ConsoleContext& ctx, const QString& arg, support::ConsoleDone done) {
      const QVector<ConnectTarget> targets = parseConnectArgs(arg);
      if (targets.isEmpty())
        return done(ConsoleOutput::error(QStringLiteral("connect needs a server URL — e.g. '/connect http://host:8090 [token]'")));
      connectEach(ctx.host, targets, 0, std::move(done));
    });
    t.insert(QStringLiteral("disconnect"), [](ConsoleContext& ctx, const QString& arg, support::ConsoleDone done) {
      const QString err = ctx.host.disconnect(arg.trimmed());
      done(err.isEmpty() ? ConsoleOutput::plain(ctx.host.connectionsRows()) : ConsoleOutput::error(err));
    });
    t.insert(QStringLiteral("connections"), [](ConsoleContext& ctx, const QString&, support::ConsoleDone done) {
      done(ConsoleOutput::plain(ctx.host.connectionsRows()));
    });
    t.insert(QStringLiteral("projects"), [](ConsoleContext& ctx, const QString& arg, support::ConsoleDone done) {
      ctx.host.projectsThen(arg.trimmed(), false, [done](const QStringList& rows) { done(ConsoleOutput::plain(rows)); });
    });
    t.insert(QStringLiteral("ls"), [](ConsoleContext& ctx, const QString&, support::ConsoleDone done) {
      done(ConsoleOutput::plain(ctx.host.localProjectsRows(false)));
    });
    t.insert(QStringLiteral("ll"), [](ConsoleContext& ctx, const QString&, support::ConsoleDone done) {
      done(ConsoleOutput::plain(ctx.host.localProjectsRows(true)));
    });
    t.insert(QStringLiteral("open"), [](ConsoleContext& ctx, const QString& arg, support::ConsoleDone done) {
      if (arg.isEmpty()) return done(ConsoleOutput::error(QStringLiteral("open takes a project name or id, a file path or a picture URL")));
      ctx.host.open(arg.trimmed(), std::move(done));
    });
    t.insert(QStringLiteral("open-window"), [](ConsoleContext& ctx, const QString& arg, support::ConsoleDone done) {
      if (arg.isEmpty()) return done(ConsoleOutput::error(QStringLiteral("open-window takes a saved project's name or id")));
      const QString err = ctx.host.openInNewWindow(arg.trimmed());
      done(err.isEmpty() ? ConsoleOutput::plain(QStringLiteral("opened in a new window")) : ConsoleOutput::error(err));
    });
    t.insert(QStringLiteral("fetch"), [](ConsoleContext& ctx, const QString& arg, support::ConsoleDone done) {
      const QStringList words = support::splitConsoleWords(arg);
      if (words.isEmpty()) return done(ConsoleOutput::error(QStringLiteral("fetch takes a server project's name — '/projects' lists them")));
      ctx.host.fetchThen(words.at(0), words.value(1), std::move(done));
    });
    t.insert(QStringLiteral("sync"), [](ConsoleContext& ctx, const QString& arg, support::ConsoleDone done) {
      const QString word = arg.trimmed().toLower();
      if (!word.isEmpty() && word != QLatin1String("on") && word != QLatin1String("off"))
        return done(ConsoleOutput::error(QStringLiteral("sync takes 'on' or 'off'")));
      if (!word.isEmpty()) ctx.host.setSyncToServer(word == QLatin1String("on"));
      done(ConsoleOutput::plain(QStringLiteral("sync to server: %1").arg(ctx.host.syncToServer() ? "on" : "off")));
    });
    t.insert(QStringLiteral("browser"), [](ConsoleContext& ctx, const QString& arg, support::ConsoleDone done) {
      const QString word = arg.trimmed().toLower();
      if (!word.isEmpty() && word != QLatin1String("incognito"))
        return done(ConsoleOutput::error(QStringLiteral("browser takes '[incognito]'")));
      ctx.host.browser(word == QLatin1String("incognito"), std::move(done));
    });
    t.insert(QStringLiteral("prompt"), [](ConsoleContext& ctx, const QString& arg, support::ConsoleDone done) {
      if (arg.isEmpty())
        return done(ConsoleOutput::error(QStringLiteral("prompt needs text — e.g. '/prompt rotate this left and make it b&w'")));
      const QString err = ctx.host.prompt(arg);
      done(err.isEmpty() ? ConsoleOutput::plain(QStringLiteral("sent to the assistant — the reply lands in the Assistant panel"))
                         : ConsoleOutput::error(err));
    });
    t.insert(QStringLiteral("llm"), [](ConsoleContext& ctx, const QString& arg, support::ConsoleDone done) {
      const QStringList words = support::splitConsoleWords(arg);
      QString err;
      const QStringList lines = ctx.host.llm(words.value(0), words.mid(1).join(QLatin1Char(' ')), &err);
      done(err.isEmpty() ? ConsoleOutput::plain(lines) : ConsoleOutput::error(err));
    });
    t.insert(QStringLiteral("status"), [](ConsoleContext& ctx, const QString&, support::ConsoleDone done) {
      done(ConsoleOutput::plain(ctx.host.status().text));
    });
    t.insert(QStringLiteral("theme"), [](ConsoleContext& ctx, const QString& arg, support::ConsoleDone done) {
      QString err;
      const QStringList lines = ctx.host.theme(arg.trimmed(), &err);
      done(err.isEmpty() ? ConsoleOutput::plain(lines) : ConsoleOutput::error(err));
    });
    t.insert(QStringLiteral("reveal-speed"), [](ConsoleContext& ctx, const QString& arg, support::ConsoleDone done) {
      QString err;
      const QStringList lines = ctx.host.revealSpeed(arg, &err);
      done(err.isEmpty() ? ConsoleOutput::plain(lines) : ConsoleOutput::error(err));
    });
    t.insert(QStringLiteral("clear"), [](ConsoleContext&, const QString&, support::ConsoleDone done) {
      done(ConsoleOutput::withEffect(ConsoleEffect::CLEAR));
    });
    t.insert(QStringLiteral("help"), [](ConsoleContext& ctx, const QString&, support::ConsoleDone done) {
      ConsoleOutput out;
      out.lines = support::consoleHelpLines(ctx.cli);
      done(out);
    });
    // `/exit` leaves whichever console asked: the window closes, the CLI stage goes.
    t.insert(QStringLiteral("exit"), [](ConsoleContext&, const QString&, support::ConsoleDone done) {
      done(ConsoleOutput::withEffect(ConsoleEffect::EXIT));
    });
    t.insert(QStringLiteral("clioff"), [](ConsoleContext&, const QString&, support::ConsoleDone done) {
      done(ConsoleOutput::withEffect(ConsoleEffect::EXIT));
    });
  }

}  // namespace stencil::gui
