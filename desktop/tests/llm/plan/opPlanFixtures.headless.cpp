// Walks the shared op-plan conformance corpus (common/fixtures/llm/opPlan/) against the REAL
// desktop parser — the port of browser/tests/llm/plan/opPlanFixtures.test.js. Verdict semantics: "valid" =
// parseOpPlan succeeds (a chat-only fallback counts), "invalid" = it fails; the desktop profile is
// "editor", a knownDivergence.desktop (or a tests/fixtureOverrides.json entry) replaces expect, and
// core's result document for every case is byte-compared with generated/normalized.json's desktop row.
#include "OpPlanSchema.hpp"
#include "opPlan.hpp"
#include "opRegistry.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <cstdio>
#include <map>
#include <utility>
#include <vector>

#include "../../support/check.hpp"
#include "../../support/fixtureCorpus.hpp"
#include "opPlanCorpus.hpp"

using namespace stencil::llm;

namespace {
  QJsonObject toQt(const opPlanCorpus::Value& v) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(stencil::core::json::toJson(v))).object();
  }

  // Core's result for every case the JS reference recorded for this surface, byte for byte.
  void checkGolden(const std::vector<opPlanCorpus::Case>& all) {
    std::map<std::pair<std::string, QString>, const opPlanCorpus::Case*> byName;
    for (const opPlanCorpus::Case& c : all)
      byName[{c.source, QString::fromStdString(c.fx.get("name")->text)}] = &c;
    const QJsonArray cases =
        readJsonFile(corpusPath("llm/opPlan/generated/normalized.json")).object().value("cases").toArray();
    int compared = 0, missing = 0, differ = 0;
    for (const QJsonValue& cv : cases) {
      const QJsonObject c = cv.toObject();
      for (const QJsonValue& g : c.value("results").toArray()) {
        if (!g.toObject().value("surfaces").toArray().contains(QStringLiteral("desktop"))) continue;
        const auto it = byName.find({c.value("source").toString().toStdString(), c.value("name").toString()});
        if (it == byName.end()) { ++missing; continue; }
        ++compared;
        const QByteArray got = planSchema().walk(QByteArray::fromStdString(it->second->text));
        if (got == g.toObject().value("json").toString().toUtf8()) continue;
        ++differ;
        std::printf("  [DIFF] %s\n       got: %s\n", qPrintable(it->second->label), got.left(400).constData());
      }
    }
    check(missing == 0, "every golden case is in the corpus");
    check(compared >= 560 && differ == 0,
          qPrintable(QStringLiteral("core's result matches normalized.json on %1 of %2 desktop cases")
                         .arg(compared - differ).arg(compared)));
  }
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  const std::vector<opPlanCorpus::Case> all = opPlanCorpus::load();
  // Floors per bundle, not on the total: the generated cases alone clear a combined floor,
  // so a vanished cases.json would otherwise walk green.
  std::vector<std::pair<QString, QJsonObject>> corpus;
  std::map<QString, std::string> texts;
  int hand = 0, generated = 0;
  for (const opPlanCorpus::Case& c : all) {
    if (c.source == "oracle") continue;
    (c.source == "hand" ? hand : generated) += 1;
    corpus.emplace_back(c.label, toQt(c.fx));
    texts[c.label] = c.text;
  }
  check(hand >= 180, qPrintable(QStringLiteral("hand-written cases.json holds %1 cases").arg(hand)));
  check(generated >= 400, qPrintable(QStringLiteral("generated/cases.json holds %1 cases").arg(generated)));
  check(planSchema().error().isEmpty(), qPrintable("the qrc registry resolves for desktop: " + planSchema().error()));

  static const QSet<QString> PROFILES = {"editor", "console", "bot", "mcp", "extension", "all"};
  static const QSet<QString> SURFACES = {"browser", "desktop", "cli", "pystencil",
                                          "bot", "mcp", "extension"};

  // ── corpus-shape check (port of the browser walker's first test) ──
  std::printf("corpus shape:\n");
  bool shapeOk = true;
  const auto shape = [&shapeOk](bool ok, const QString& msg) {
    if (!ok) {
      std::printf("  [FAIL] %s\n", qPrintable(msg));
      shapeOk = false;
    }
  };
  for (const auto& [file, fx] : corpus) {
    shape(!fx.isEmpty() && !file.isEmpty(), file + ": a non-empty case under a label");
    QString slug = file;
    slug.remove(QRegularExpression("^\\d+-"));
    shape(fx.value("name").toString() + ".json" == slug,
          file + ": \"name\" must match the filename slug");
    const QJsonArray profiles = fx.value("profiles").toArray();
    shape(fx.value("profiles").isArray() && !profiles.isEmpty(),
          file + ": \"profiles\" must be a non-empty array");
    for (const QJsonValue& p : profiles)
      shape(PROFILES.contains(p.toString()), file + ": unknown profile \"" + p.toString() + "\"");
    const QString expect = fx.value("expect").toString();
    shape(expect == "valid" || expect == "invalid", file + ": \"expect\" must be valid|invalid");
    shape(!fx.value("input").isUndefined() && !fx.value("input").isNull(),
          file + ": \"input\" is required");
    if (expect == "invalid")
      shape(fx.value("reason").isString() && !fx.value("reason").toString().isEmpty(),
            file + ": invalid cases need a \"reason\"");
    const QJsonObject kd = fx.value("knownDivergence").toObject();
    for (auto it = kd.begin(); it != kd.end(); ++it) {
      shape(SURFACES.contains(it.key()),
            file + ": unknown knownDivergence surface \"" + it.key() + "\"");
      const QString v = it.value().toString();
      shape(v == "valid" || v == "invalid", file + ": knownDivergence verdicts are valid|invalid");
    }
  }
  check(shapeOk, "the corpus is well-formed");

  // ── per-fixture walk (desktop profile: editor) ──
  std::printf("fixtures (profile editor):\n");
  int walked = 0, skipped = 0, overridden = 0, diverged = 0;
  for (const auto& [file, fx] : corpus) {
    bool applies = false;
    for (const QJsonValue& p : fx.value("profiles").toArray())
      if (p.toString() == "editor" || p.toString() == "all") applies = true;
    if (!applies) {
      ++skipped;
      continue;
    }
    ++walked;

    const QString name = fx.value("name").toString();
    const FixtureOverride ov = findOverride("opPlan", name);
    const QJsonValue kd = fx.value("knownDivergence").toObject().value("desktop");
    QString want = fx.value("expect").toString();
    if (kd.isString()) {
      want = kd.toString();
      ++diverged;
    }
    if (ov.present) {
      want = ov.verdict.toString();
      ++overridden;
    }

    const std::string& bytes = texts[file];
    const OpPlanResult r = parseOpPlan(QString::fromUtf8(bytes.data(), static_cast<qsizetype>(bytes.size())));
    const QString tag = ov.present ? " [override]" : kd.isString() ? " [knownDivergence]" : "";
    check(r.ok == (want == "valid"),
          qPrintable(QStringLiteral("%1 -> %2%3").arg(file, want, tag)));
    if (r.ok != (want == "valid"))
      std::printf("       desktop said %s (%s)\n", r.ok ? "valid" : "invalid",
                  qPrintable(r.error));
  }
  std::printf("  walked %d, skipped %d (profile), knownDivergence %d, local overrides %d\n",
              walked, skipped, diverged, overridden);

  std::printf("golden (generated/normalized.json, desktop):\n");
  checkGolden(all);

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
