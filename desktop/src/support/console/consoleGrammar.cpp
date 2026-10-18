#include "consoleGrammar.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace stencil::support {

  ParsedConsoleLine parseConsoleLine(const QString& line) {
    ParsedConsoleLine out;
    QString s = line.trimmed();
    out.slashed = s.startsWith(QLatin1Char('/'));
    if (out.slashed) s = s.mid(1).trimmed();
    if (s.isEmpty()) return out;
    static const QRegularExpression SPACE(QStringLiteral("\\s"));
    const int sp = s.indexOf(SPACE);
    out.word = (sp < 0 ? s : s.left(sp)).toLower();
    out.arg = sp < 0 ? QString() : s.mid(sp + 1).trimmed();
    return out;
  }

  QStringList splitConsoleWords(const QString& arg) {
    static const QRegularExpression SEP(QStringLiteral("[\\s,]+"));
    return arg.split(SEP, Qt::SkipEmptyParts);
  }

  namespace {
    struct Table {
      QVector<CommandSpec> rows;
      QStringList sections;
      CliModeConfig cli;
    };
    const Table& table() {
      static const Table t = [] {
        Table out;
        QFile f(QStringLiteral(":/config/console/commands.json"));
        if (!f.open(QIODevice::ReadOnly)) return out;
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        for (const QJsonValue& s : root.value(QLatin1String("sections")).toArray()) out.sections << s.toString();
        const QJsonObject cli = root.value(QLatin1String("cliMode")).toObject();
        out.cli.typedWord = cli.value(QLatin1String("typedWord")).toString().toLower();
        out.cli.holdMs = cli.value(QLatin1String("holdMs")).toInt(out.cli.holdMs);
        out.cli.offCommand = cli.value(QLatin1String("offCommand")).toString();
        for (const QJsonValue& v : root.value(QLatin1String("commands")).toArray()) {
          const QJsonObject o = v.toObject();
          bool here = false;
          for (const QJsonValue& s : o.value(QLatin1String("surfaces")).toArray())
            here = here || s.toString() == QLatin1String("desktop");
          if (!here) continue;
          CommandSpec c;
          c.name = o.value(QLatin1String("name")).toString();
          for (const QJsonValue& a : o.value(QLatin1String("aliases")).toArray()) c.aliases << a.toString();
          c.arg = o.value(QLatin1String("arg")).toString();
          c.help = o.value(QLatin1String("help")).toString();
          c.section = o.value(QLatin1String("section")).toString();
          c.cliMode = o.value(QLatin1String("cliMode")).toBool(false);
          out.rows.push_back(c);
        }
        return out;
      }();
      return t;
    }
  }  // namespace

  const QVector<CommandSpec>& consoleCommandTable() { return table().rows; }
  const QStringList& consoleSections() { return table().sections; }
  const CliModeConfig& cliModeConfig() { return table().cli; }

  const CommandSpec* resolveConsoleCommand(const QString& word, bool cliMode) {
    const QString w = word.trimmed().toLower();
    if (w.isEmpty()) return nullptr;
    for (const CommandSpec& c : table().rows) {
      if (c.cliMode && !cliMode) continue;
      if (c.name == w || c.aliases.contains(w)) return &c;
    }
    return nullptr;
  }

  QStringList consoleCompletions(const QString& prefix, bool cliMode) {
    const QString p = prefix.trimmed().toLower();
    QStringList out;
    for (const CommandSpec& c : table().rows)
      if ((cliMode || !c.cliMode) && c.name.startsWith(p)) out << c.name;
    return out;
  }

  QVector<ConsoleLine> consoleHelpLines(bool cliMode) {
    QVector<ConsoleLine> out;
    out.push_back({QStringLiteral("Commands  (a leading '/' is optional)"), ConsoleKind::PLAIN});
    for (const QString& section : table().sections) {
      bool any = false;
      for (const CommandSpec& c : table().rows) {
        if (c.section != section || (c.cliMode && !cliMode)) continue;
        if (!any) {
          out.push_back({QString(), ConsoleKind::PLAIN});
          out.push_back({section, ConsoleKind::SECTION});
          any = true;
        }
        const QString head = QLatin1Char('/') + c.name + (c.arg.isEmpty() ? QString() : QLatin1Char(' ') + c.arg);
        // A head past the gutter still keeps one space before its help, as the Zig pins show.
        out.push_back({(head.size() >= HELP_GUTTER ? head + QLatin1Char(' ') : head.leftJustified(HELP_GUTTER)) + c.help, ConsoleKind::PLAIN, int(head.size())});
      }
    }
    if (cliMode) {
      out.push_back({QString(), ConsoleKind::PLAIN});
      out.push_back({QStringLiteral("'/%1' or holding the logo leaves CLI mode").arg(table().cli.offCommand), ConsoleKind::PLAIN});
    }
    return out;
  }

  bool consoleTypesKey(const QString& line) {
    const ParsedConsoleLine p = parseConsoleLine(line);
    const QStringList words = splitConsoleWords(p.arg);
    return p.word == QLatin1String("llm") && words.size() >= 2 && words.first().toLower() == QLatin1String("key");
  }

  QString consoleEchoText(const QString& line) { return consoleTypesKey(line) ? QStringLiteral("/llm key ••••••••") : line; }

  QString consoleHistoryText(const QString& line) { return consoleTypesKey(line) ? QStringLiteral("/llm key") : line; }

}  // namespace stencil::support
