// Walks the shared provider-wire and sanitizer conformance corpora (browser/js/config/llm/fixtures/
// providerWire + sanitizer) against the REAL desktop LLM client through a capturing MockTransport (no
// network) — the desktop counterpart of browser/tests/llmWireFixtures.test.js. The fixtures carry a
// literal `chat.system`, so the walker passes it AS the suffix and substitutes it back before the deep
// body compare. LlmClient.cpp is #included so the file-local sanitizeProviderText is walkable.
#include "LlmClient.cpp"  // NOLINT — grants access to the anon-namespace sanitizer

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

#include "../../support/check.hpp"
#include "../../support/fixtureCorpus.hpp"

using namespace stencil::llm;
void walkWireFile(const char* rel, int& walked, int& overridden);

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  int walked = 0, overridden = 0;
  walkWireFile("llm/fixtures/providerWire/ollama.json", walked, overridden);
  walkWireFile("llm/fixtures/providerWire/openai.json", walked, overridden);
  walkWireFile("llm/fixtures/providerWire/server.json", walked, overridden);
  walkWireFile("llm/fixtures/providerWire/httpErrors.json", walked, overridden);
  std::printf("providerWire: walked %d cases, %d local overrides\n", walked, overridden);

  // Sanitizer vectors: `expect` is the BROWSER's output, and a measured desktop difference is pinned in
  // the override map. Three invariants hold for EVERY case: no URL, no 24+ token-shaped run, ≤200 QChars.
  std::printf("sanitizer:\n");
  int sanWalked = 0, sanOverridden = 0, sanSkipped = 0;
  {
    bool ok = false;
    const QJsonArray cases =
        readJsonFile(corpusPath("llm/fixtures/sanitizer/cases.json"), &ok).array();
    check(ok && !cases.isEmpty(), "sanitizer/cases.json loads");
    static const QRegularExpression urlRe(
        QStringLiteral(R"([a-zA-Z][a-zA-Z0-9+.-]*://\S+)"));
    static const QRegularExpression tokenRun(QStringLiteral("[A-Za-z0-9_-]{24,}"));
    for (const QJsonValue& cv : cases) {
      const QJsonObject c = cv.toObject();
      const QString name = c.value("name").toString();
      const QJsonValue input = c.value("input");
      if (input.isNull()) {  // QString has no null/absent distinction worth pinning
        ++sanSkipped;
        std::printf("  (skip) %s — null input reads as QString(), expect \"\"\n",
                    qPrintable(name));
        check(sanitizeProviderText(QString()).isEmpty(), "null/empty input sanitizes to empty");
        continue;
      }
      ++sanWalked;
      const FixtureOverride ov = findOverride("sanitizer", name);
      if (ov.present) ++sanOverridden;
      const QString want = ov.present ? ov.verdict.toString() : c.value("expect").toString();
      const QString got = sanitizeProviderText(input.toString());
      check(got == want,
            qPrintable(name + (ov.present ? QStringLiteral(" [override]") : QString())));
      if (got != want)
        std::printf("       got: \"%s\"\n", qPrintable(got));
      check(got.size() <= 200 && !urlRe.match(got).hasMatch() &&
                !tokenRun.match(got).hasMatch(),
            qPrintable(QStringLiteral("%1: invariants (<=200, no URL, no token run)").arg(name)));
    }
  }
  std::printf("sanitizer: walked %d cases, %d local overrides, %d skipped\n",
              sanWalked, sanOverridden, sanSkipped);

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
