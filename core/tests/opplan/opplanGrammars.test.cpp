// The hand-matched grammars against JS RegExp: every probe in generated/grammarProbes.json must
// match exactly the regexes JS said it matches, and each grammar's recorded source must still
// be the registry's — a regex edited in opRegistry.json alone fails here, natively.
#include "doctest.h"

#include "opplanCorpus.hpp"
#include "planGrammars.hpp"
#include "planSchema.hpp"

#include <string>

using stencil::core::json::Value;
using namespace stencil::core::opplan;

TEST_CASE("opplan grammars: every probe matches what JS RegExp.test said") {
  const Value doc = opplanCorpus::readFile(opplanCorpus::llmDir() / "fixtures/opPlan/generated/grammarProbes.json");
  const Value* probes = doc.get("probes");
  REQUIRE(probes);
  CHECK(probes->items.size() >= 2000);
  std::size_t hits = 0;
  for (const Value& row : probes->items) {
    const std::string& s = row.items[0].text;
    for (std::size_t g = 0; g < grammarCount(); ++g) {
      const GrammarRow& grammar = grammarAt(g);
      bool want = false;
      for (const Value& name : row.items[1].items) want = want || name.text == grammar.name;
      CHECK_MESSAGE(matches(grammar.grammar, s) == want, grammar.name << " on \"" << s << "\"");
      hits += want ? 1 : 0;
    }
  }
  CHECK(hits > 100);
}

TEST_CASE("opplan twin drift: the grammar sources and the JSON caps are the registry's") {
  const Value registry = opplanCorpus::readFile(opplanCorpus::llmDir() / "opRegistry.json");
  const Value* regexes = registry.get("regexes");
  REQUIRE(regexes);
  std::size_t named = 0;
  for (std::size_t i = 0; i < regexes->keys.size(); ++i) {
    const std::string& name = regexes->keys[i];
    if (name == "describe" || name == "note") continue;
    ++named;
    const GrammarRow* row = grammarNamed(name);
    REQUIRE_MESSAGE(row, "core matches no grammar named " << name);
    CHECK_MESSAGE(regexes->items[i].text == row->source, name << " drifted from core/opplan/planGrammars");
  }
  CHECK(named == grammarCount());
  const Value* caps = registry.get("limits")->get("json");
  REQUIRE(caps);
  CHECK(caps->get("MAX_BYTES")->number == static_cast<double>(REGISTRY_CAPS.bytes));
  CHECK(caps->get("MAX_DEPTH")->number == static_cast<double>(REGISTRY_CAPS.depth));
  CHECK(caps->get("MAX_NODES")->number == static_cast<double>(REGISTRY_CAPS.nodes));
}

TEST_CASE("opplan grammars: the edges the JS reference draws") {
  CHECK(matches(Grammar::CROP_TOKEN, "-.5in"));
  CHECK_FALSE(matches(Grammar::CROP_TOKEN, "1."));
  CHECK_FALSE(matches(Grammar::CROP_TOKEN, "\xd9\xa3"));
  CHECK(matches(Grammar::CROP_ASPECT, "007:1"));
  CHECK_FALSE(matches(Grammar::CROP_ASPECT, "00:1"));
  CHECK(matches(Grammar::PAGE_FORMAT, "c10"));
  CHECK_FALSE(matches(Grammar::PAGE_FORMAT, "a11"));
  CHECK(matches(Grammar::HTTP_URL, "HTTPS://a\xe2\x80\x8b"));
  CHECK_FALSE(matches(Grammar::HTTP_URL, "https://a\xc2\xa0"));
  CHECK_FALSE(matches(Grammar::HTTP_URL, "https://"));
  CHECK(matches(Grammar::URL_SCHEME, "z9+.-://"));
  CHECK_FALSE(matches(Grammar::URL_SCHEME, "9z://"));
}
