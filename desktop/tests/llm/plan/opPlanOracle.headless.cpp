// The typed op-plan oracle: parseOpPlan's whole result — verdict, reply, warnings, error and the
// typed actions / variants / ask card — over every hand-written, generated and adversarial case,
// pinned in tests/pins/opPlanOracle.json. STENCIL_UPDATE_ORACLE=1 re-records it. Twin of
// pystencil's tests/llm/plan/test_llm_plan_oracle.py.
#include "opPlan.hpp"
#include "opRegistry.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>
#include <map>

#include "../../support/check.hpp"
#include "opPlanCorpus.hpp"

using namespace stencil::llm;

namespace {

  // A string past this many UTF-16 units is pinned by its length and digest.
  constexpr int LONG = 160;

  QJsonValue pin(const QJsonValue& v) {
    if (v.isString() && v.toString().size() > LONG) {
      const QByteArray digest = QCryptographicHash::hash(v.toString().toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
      return QStringLiteral("<%1 chars sha256:%2>").arg(v.toString().size()).arg(QString::fromLatin1(digest));
    }
    if (v.isArray()) {
      QJsonArray out;
      for (const QJsonValue& x : v.toArray()) out.append(pin(x));
      return out;
    }
    if (v.isObject()) {
      QJsonObject out;
      const QJsonObject o = v.toObject();
      for (auto it = o.begin(); it != o.end(); ++it) out.insert(it.key(), pin(it.value()));
      return out;
    }
    return v;
  }

  QJsonArray linesJson(const stencil::core::Lines& lines) {
    QJsonArray out;
    for (const auto& l : lines) {
      QJsonArray pts;
      for (const auto& p : l.points) pts.append(QJsonArray{p.x, p.y});
      out.append(QJsonObject{{"points", pts}, {"color", QString::fromStdString(l.color)},
                             {"thickness", l.thickness}, {"pointSize", l.pointSize},
                             {"style", QString::fromStdString(l.style)}, {"locked", l.locked},
                             {"fillColor", QString::fromStdString(l.fillColor)},
                             {"pointColor", QString::fromStdString(l.pointColor)}});
    }
    return out;
  }

  // Every field that differs from a default Action, plus the op.
  QJsonObject actionJson(const Action& a) {
    static const Action d;
    QJsonObject o{{"op", opName(a.op)}};
    const auto s = [&](const char* k, const QString& v, const QString& dv) { if (v != dv) o.insert(k, v); };
    const auto n = [&](const char* k, double v, double dv) { if (v != dv) o.insert(k, v); };
    const auto b = [&](const char* k, bool v, bool dv) { if (v != dv) o.insert(k, v); };
    s("x1", a.x1, d.x1); s("x2", a.x2, d.x2); s("y1", a.y1, d.y1); s("y2", a.y2, d.y2);
    s("aspect", a.aspect, d.aspect); b("rotateLeft", a.rotateLeft, d.rotateLeft); n("times", a.times, d.times);
    s("mode", a.mode, d.mode); s("tint", a.tint, d.tint);
    if (!a.lines.empty()) o.insert("lines", linesJson(a.lines));
    if (!a.axis.isNull()) o.insert("axis", QString(a.axis));
    s("expr", a.expr, d.expr); n("formulaEnabled", a.formulaEnabled, d.formulaEnabled);
    s("format", a.format, d.format); s("color", a.color, d.color);
    n("widthCm", a.widthCm, d.widthCm); n("heightCm", a.heightCm, d.heightCm);
    if (!a.indices.isEmpty()) {
      QJsonArray idx;
      for (int i : a.indices) idx.append(i);
      o.insert("indices", idx);
    }
    n("steps", a.steps, d.steps); n("thickness", a.thickness, d.thickness); n("pointSize", a.pointSize, d.pointSize);
    s("style", a.style, d.style); s("pointColor", a.pointColor, d.pointColor);
    b("pointColorSet", a.pointColorSet, d.pointColorSet); s("drawMode", a.drawMode, d.drawMode);
    s("fillColor", a.fillColor, d.fillColor); s("value", a.value, d.value); n("split", a.split, d.split);
    n("percent", a.percent, d.percent); b("fit", a.fit, d.fit); b("current", a.current, d.current);
    s("what", a.what, d.what); s("preset", a.preset, d.preset);
    n("viewPoints", a.viewPoints, d.viewPoints); n("viewLines", a.viewLines, d.viewLines);
    n("chatOpen", a.chatOpen, d.chatOpen); s("dock", a.dock, d.dock); s("dialog", a.dialog, d.dialog);
    s("server", a.server, d.server); s("url", a.url, d.url); b("incognito", a.incognito, d.incognito);
    s("path", a.path, d.path); n("index", a.index, d.index); s("name", a.name, d.name);
    return o;
  }

  QJsonArray actionsJson(const QVector<Action>& list) {
    QJsonArray out;
    for (const Action& a : list) out.append(actionJson(a));
    return out;
  }

  QJsonObject typedResult(const std::string& text) {
    const OpPlanResult r = parseOpPlan(QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())));
    if (!r.ok) return pin(QJsonObject{{"verdict", "invalid"}, {"error", r.error}}).toObject();
    const OpPlan& p = r.plan;
    QJsonArray variants;
    for (const Variant& v : p.variants) variants.append(QJsonObject{{"label", v.label}, {"actions", actionsJson(v.actions)}});
    QJsonObject out{{"verdict", p.chatOnly ? "chatOnly" : "valid"}, {"reply", p.reply},
                    {"warnings", QJsonArray::fromStringList(p.warnings)}, {"actions", actionsJson(p.actions)},
                    {"variants", variants}};
    if (!p.ask.options.isEmpty()) {
      QJsonArray options;
      for (const AskOption& o : p.ask.options)
        options.append(QJsonObject{{"label", o.label}, {"actions", actionsJson(o.actions)},
                                   {"imageUrl", o.imageUrl}, {"projectId", o.projectId}});
      out.insert("ask", QJsonObject{{"question", p.ask.question}, {"multi", p.ask.multi},
                                    {"allowCustom", p.ask.allowCustom}, {"customLabel", p.ask.customLabel},
                                    {"options", options}});
    }
    return pin(out).toObject();
  }

  QByteArray compact(const QJsonObject& o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const QString golden = QStringLiteral(STENCIL_ORACLE_JSON);

  std::map<QString, QJsonObject> results;
  for (const opPlanCorpus::Case& c : opPlanCorpus::load()) results[c.label] = typedResult(c.text);
  check(results.size() >= 690, qPrintable(QStringLiteral("the corpus holds %1 cases").arg(results.size())));

  if (qEnvironmentVariableIntValue("STENCIL_UPDATE_ORACLE") == 1) {
    QByteArray out = "{\n";
    bool first = true;
    for (const auto& [name, r] : results) {
      if (!first) out += ",\n";
      first = false;
      out += QJsonDocument(QJsonArray{name}).toJson(QJsonDocument::Compact).mid(1).chopped(1) + ": " + compact(r);
    }
    out += "\n}\n";
    QFile f(golden);
    check(f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(out) == out.size(), "re-recorded the oracle");
    std::printf("\nre-recorded %s\n", qPrintable(golden));
    return failures ? 1 : 0;
  }

  const QJsonObject want = readJsonFile(golden).object();
  check(!want.isEmpty(), "the recorded oracle loads (STENCIL_UPDATE_ORACLE=1 records it)");
  int differ = 0;
  for (const auto& [name, got] : results) {
    const QByteArray g = compact(got), w = compact(want.value(name).toObject());
    if (g == w) continue;
    ++differ;
    std::printf("  [DIFF] %s\n       got: %s\n      want: %s\n", qPrintable(name), g.left(600).constData(), w.left(600).constData());
  }
  check(differ == 0, qPrintable(QStringLiteral("%1 of %2 typed results match the oracle").arg(results.size() - differ).arg(results.size())));
  check(static_cast<std::size_t>(want.size()) == results.size(), "the oracle holds exactly the corpus's cases");

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
