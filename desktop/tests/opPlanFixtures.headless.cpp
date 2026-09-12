// Walks the shared op-plan conformance corpus (browser/js/config/llm/fixtures/
// opPlan/) against the REAL desktop validator (src/llm/opPlan.cpp) — the
// desktop port of the reference walker browser/tests/opPlanFixtures.test.js.
//
// Verdict semantics: "valid" = parseOpPlan succeeds (a chat-only fallback
// counts), "invalid" = it fails. Object `input` is serialized compact and fed
// as a model reply would arrive; string `input` is fed verbatim. The desktop
// profile is "editor"; a fixture's knownDivergence.desktop — or a local
// tests/fixtureOverrides.json entry, which wins — replaces `expect`.
#include "opPlan.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <cstdio>
#include <utility>
#include <vector>

#include "support/check.hpp"
#include "support/fixtureCorpus.hpp"

using namespace stencil::llm;

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  const QString dir = corpusPath("llm/fixtures/opPlan");
  // The hand-written bundle (each case carrying its stable "file" label) plus the
  // registry-generated one (generated/cases.json, browser/tools/genOpPlanFixtures.mjs),
  // whose cases walk as "<name>.json".
  const QJsonArray hand = readJsonFile(dir + "/cases.json").object().value("cases").toArray();
  const QJsonArray generated =
      readJsonFile(dir + "/generated/cases.json").object().value("cases").toArray();
  std::vector<std::pair<QString, QJsonObject>> corpus;
  for (const QJsonValue& c : hand)
    corpus.emplace_back(c.toObject().value("file").toString(), c.toObject());
  for (const QJsonValue& c : generated)
    corpus.emplace_back(c.toObject().value("name").toString() + ".json", c.toObject());
  // Floors per bundle, not on the total: the generated cases alone clear a combined floor,
  // so a vanished cases.json would otherwise walk green.
  check(hand.size() >= 180,
        qPrintable(QStringLiteral("hand-written cases.json holds %1 cases").arg(hand.size())));
  check(generated.size() >= 400,
        qPrintable(QStringLiteral("generated/cases.json holds %1 cases").arg(generated.size())));

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

    const QJsonValue input = fx.value("input");
    const QString text = input.isString()
        ? input.toString()
        : QString::fromUtf8(QJsonDocument(input.toObject()).toJson(QJsonDocument::Compact));

    const OpPlanResult r = parseOpPlan(text);
    const QString tag = ov.present ? " [override]" : kd.isString() ? " [knownDivergence]" : "";
    check(r.ok == (want == "valid"),
          qPrintable(QStringLiteral("%1 -> %2%3").arg(file, want, tag)));
    if (r.ok != (want == "valid"))
      std::printf("       desktop said %s (%s)\n", r.ok ? "valid" : "invalid",
                  qPrintable(r.error));
  }
  std::printf("  walked %d, skipped %d (profile), knownDivergence %d, local overrides %d\n",
              walked, skipped, diverged, overridden);

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
