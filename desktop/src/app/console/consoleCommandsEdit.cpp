// The edit half of the console's commands: what brings a picture in, the transforms, the
// colours and the saves. Each is a ConsoleHost flow behind a line of argument parsing.
#include "consoleCommands.hpp"
#include "ConsoleHost.hpp"
#include "consoleGrammar.hpp"

#include <QRegularExpression>

namespace stencil::gui {

  using support::ConsoleOutput;

  namespace {
    ConsoleOutput lineOf(const QString& text) { return ConsoleOutput::plain(text); }

    // The crop, filter, layout and script rows run a one-line .stc program, so the grammar and
    // the messages are the contract's (contracts/stc/stc-contract.md §3).
    void viaScript(ConsoleContext& ctx, const QString& program, const QString& ok, support::ConsoleDone done) {
      ctx.host.runScript(program, [ok, done](const ConsoleOutput& out) {
        done(out.lines.isEmpty() ? lineOf(ok) : out);
      });
    }

    void colourRow(ConsoleContext& ctx, const QString& arg, const QString& what,
                   const std::function<QString(const QString&)>& set, const std::function<QString()>& get,
                   const support::ConsoleDone& done) {
      if (!arg.isEmpty()) {
        const QString err = set(arg.trimmed().toLower() == QLatin1String("clear") ? QString() : arg.trimmed());
        if (!err.isEmpty()) return done(ConsoleOutput::error(err));
      }
      Q_UNUSED(ctx);
      done(lineOf(QStringLiteral("%1: %2").arg(what, get())));
    }
  }  // namespace

  void registerEditCommands(QHash<QString, ConsoleHandler>& t) {
    t.insert(QStringLiteral("upload"), [](ConsoleContext& ctx, const QString& arg, support::ConsoleDone done) {
      ctx.host.upload(arg, std::move(done));
    });
    t.insert(QStringLiteral("blank"), [](ConsoleContext& ctx, const QString& arg, support::ConsoleDone done) {
      const QString err = ctx.host.blank(support::splitConsoleWords(arg));
      done(err.isEmpty() ? lineOf(QStringLiteral("blank page ready")) : ConsoleOutput::error(err));
    });
    t.insert(QStringLiteral("format"), [](ConsoleContext& ctx, const QString& arg, support::ConsoleDone done) {
      QString err;
      const QStringList lines = ctx.host.format(support::splitConsoleWords(arg), &err);
      done(err.isEmpty() ? ConsoleOutput::plain(lines) : ConsoleOutput::error(err));
    });
    t.insert(QStringLiteral("apply"), [](ConsoleContext& ctx, const QString& arg, support::ConsoleDone done) {
      if (!needImage(ctx, done)) return;
      if (arg.isEmpty())
        return done(ConsoleOutput::error(QStringLiteral("apply needs a layout: '/apply <path|url>' or '/apply {\"lines\":[…]}'")));
      if (arg.trimmed().startsWith(QLatin1Char('{'))) return ctx.host.applyLayoutJson(arg, std::move(done));
      viaScript(ctx, QStringLiteral("@layout ") + arg, QStringLiteral("layout drawn"), std::move(done));
    });
    t.insert(QStringLiteral("crop"), [](ConsoleContext& ctx, const QString& arg, support::ConsoleDone done) {
      if (!needImage(ctx, done)) return;
      if (arg.isEmpty())
        return done(ConsoleOutput::error(QStringLiteral("crop takes edges — e.g. '/crop x1=10% x2=90% y1=10% y2=90%' (units: px, cm, mm, in, %)")));
      viaScript(ctx, QStringLiteral("@crop ") + arg, QStringLiteral("cropped"), std::move(done));
    });
    t.insert(QStringLiteral("rotate"), [](ConsoleContext& ctx, const QString& arg, support::ConsoleDone done) {
      if (!needImage(ctx, done)) return;
      bool ok = false;
      const int n = arg.toInt(&ok);
      if (!ok) return done(ConsoleOutput::error(QStringLiteral("rotate takes a whole number of quarter turns — e.g. '/rotate 1', '/rotate -1'")));
      const int turns = ((n % 4) + 4) % 4;
      if (turns == 0) return done(lineOf(QStringLiteral("no change (a full turn)")));
      ctx.host.rotate(turns);
      done(lineOf(QStringLiteral("rotated %1°").arg(n * 90)));
    });
    t.insert(QStringLiteral("flip"), [](ConsoleContext& ctx, const QString&, support::ConsoleDone done) {
      if (!needImage(ctx, done)) return;
      ctx.host.flip();
      done(lineOf(QStringLiteral("flipped")));
    });
    t.insert(QStringLiteral("filter"), [](ConsoleContext& ctx, const QString& arg, support::ConsoleDone done) {
      if (!needImage(ctx, done)) return;
      if (arg.isEmpty()) return done(lineOf(QStringLiteral("filter: ") + ctx.host.filterLabel()));
      viaScript(ctx, QStringLiteral("@filter ") + arg, QStringLiteral("filter: ") + arg.trimmed().toLower(), std::move(done));
    });
    t.insert(QStringLiteral("script"), [](ConsoleContext& ctx, const QString& arg, support::ConsoleDone done) {
      if (arg.isEmpty())
        return done(ConsoleOutput::error(QStringLiteral("script takes .stc directives — e.g. '/script @crop 10%; @filter bw'")));
      static const QRegularExpression SEMI(QStringLiteral("\\s*;\\s*"));
      viaScript(ctx, arg.split(SEMI, Qt::SkipEmptyParts).join(QLatin1Char('\n')), QStringLiteral("script ran"), std::move(done));
    });
    t.insert(QStringLiteral("undo"), [](ConsoleContext& ctx, const QString&, support::ConsoleDone done) {
      done(lineOf(ctx.host.step(false) ? QStringLiteral("undone") : QStringLiteral("nothing to undo")));
    });
    t.insert(QStringLiteral("redo"), [](ConsoleContext& ctx, const QString&, support::ConsoleDone done) {
      done(lineOf(ctx.host.step(true) ? QStringLiteral("redone") : QStringLiteral("nothing to redo")));
    });
    t.insert(QStringLiteral("reset"), [](ConsoleContext& ctx, const QString&, support::ConsoleDone done) {
      const int n = ctx.host.undoAll();
      done(lineOf(n ? QStringLiteral("reset: %1 edit%2 undone").arg(n).arg(n == 1 ? "" : "s")
                    : QStringLiteral("nothing to undo (at the original)")));
    });
    t.insert(QStringLiteral("color"), [](ConsoleContext& ctx, const QString& arg, support::ConsoleDone done) {
      colourRow(ctx, arg, QStringLiteral("line colour"), [&ctx](const QString& c) { return ctx.host.setLineColor(c); },
                [&ctx] { return ctx.host.lineColor(); }, done);
    });
    t.insert(QStringLiteral("point-color"), [](ConsoleContext& ctx, const QString& arg, support::ConsoleDone done) {
      colourRow(ctx, arg, QStringLiteral("point colour"), [&ctx](const QString& c) { return ctx.host.setPointColor(c); },
                [&ctx] { return ctx.host.pointColor(); }, done);
    });
    t.insert(QStringLiteral("project-color"), [](ConsoleContext& ctx, const QString& arg, support::ConsoleDone done) {
      colourRow(ctx, arg, QStringLiteral("project colour"), [&ctx](const QString& c) { return ctx.host.setProjectColor(c); },
                [&ctx] { return ctx.host.projectColor(); }, done);
    });
    t.insert(QStringLiteral("save"), [](ConsoleContext& ctx, const QString& arg, support::ConsoleDone done) {
      if (!needImage(ctx, done)) return;
      ctx.host.save(arg.trimmed(), std::move(done));
    });
    t.insert(QStringLiteral("layout"), [](ConsoleContext& ctx, const QString&, support::ConsoleDone done) {
      if (!needImage(ctx, done)) return;
      ctx.host.saveLayout();
      done(lineOf(QStringLiteral("layout saved")));
    });
  }

}  // namespace stencil::gui
