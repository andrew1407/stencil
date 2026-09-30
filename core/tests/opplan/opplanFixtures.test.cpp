// core/opplan against the shared op-plan corpus: every case (hand-written, registry-generated,
// adversarial oracle) byte-compared with generated/normalized.json — the JS reference walk's own
// output — for every core surface, the corpus verdicts with their knownDivergence, and replies
// cut at every seventh byte, which must never crash a surface.
#include "doctest.h"

#include "opplanCorpus.hpp"
#include "planSchema.hpp"
#include "planWalk.hpp"

#include <map>
#include <memory>
#include <string>

using stencil::core::json::Value;
using stencil::core::opplan::Schema;
using stencil::core::opplan::Status;
using stencil::core::opplan::walkPlan;

namespace {
  const char* const SURFACES[] = {"browser", "desktop", "cli", "pystencil", "bot", "mcp"};

  std::map<std::string, std::unique_ptr<Schema>> schemas() {
    const std::string registry = opplanCorpus::registryText();
    std::map<std::string, std::unique_ptr<Schema>> out;
    for (const char* surface : SURFACES) {
      auto s = std::make_unique<Schema>();
      s->load(registry, surface, nullptr);
      out[surface] = std::move(s);
    }
    return out;
  }
}  // namespace

TEST_CASE("opplan golden: every case on every core surface is byte-equal to the JS reference") {
  const auto all = schemas();
  for (const auto& row : all) REQUIRE_MESSAGE(row.second->error.empty(), row.first << ": " << row.second->error);
  const auto sources = opplanCorpus::sources();
  const Value golden = opplanCorpus::readFile(opplanCorpus::llmFixturesDir() / "opPlan/generated/normalized.json");
  const Value* cases = golden.get("cases");
  REQUIRE(cases);
  CHECK(cases->items.size() >= 690);
  std::size_t compared = 0;
  for (const Value& c : cases->items) {
    const std::string& source = c.get("source")->text;
    const std::string& name = c.get("name")->text;
    const auto bundle = sources.find(source);
    REQUIRE_MESSAGE(bundle != sources.end(), source);
    const auto found = bundle->second.find(name);
    REQUIRE_MESSAGE(found != bundle->second.end(), source << "/" << name << " is in the golden but not the corpus");
    const std::string text = opplanCorpus::caseText(found->second);
    for (const Value& group : c.get("results")->items)
      for (const Value& surface : group.get("surfaces")->items) {
        const auto result = walkPlan(*all.at(surface.text), text);
        CHECK_MESSAGE(result.text == group.get("json")->text, source << "/" << name << " on " << surface.text);
        ++compared;
      }
  }
  CHECK(compared >= 2500);
}

TEST_CASE("opplan corpus: every case's verdict per surface, knownDivergence honoured") {
  const auto all = schemas();
  const auto sources = opplanCorpus::sources();
  std::size_t walked = 0;
  for (const char* source : {"hand", "generated"}) {
    for (const auto& one : sources.at(source)) {
      const std::string& name = one.first;
      const Value& c = one.second;
      for (const auto& row : all) {
        const std::string& surface = row.first;
        const Schema& s = *row.second;
        bool applies = false;
        for (const Value& p : c.get("profiles")->items) applies = applies || p.text == "all" || p.text == s.profile;
        if (!applies) continue;
        std::string want = c.get("expect")->text;
        if (const Value* kd = c.get("knownDivergence"))
          if (const Value* v = kd->get(surface)) want = v->text;
        const auto result = walkPlan(s, opplanCorpus::caseText(c));
        CHECK_MESSAGE((result.status == Status::INVALID ? "invalid" : "valid") == want, name << " on " << surface);
        ++walked;
      }
    }
  }
  CHECK(walked >= 2300);
}

TEST_CASE("opplan: a reply cut at every seventh byte never crashes a surface") {
  const auto all = schemas();
  const auto sources = opplanCorpus::sources();
  std::size_t cuts = 0;
  for (const char* source : {"hand", "oracle"}) {
    for (const auto& one : sources.at(source)) {
      const std::string text = opplanCorpus::caseText(one.second);
      if (text.size() > 4096) continue;
      for (std::size_t cut = 0; cut <= text.size(); cut += 7) {
        const auto result = walkPlan(*all.at("cli"), std::string_view(text).substr(0, cut));
        CHECK(!result.text.empty());
        ++cuts;
      }
    }
  }
  CHECK(cuts > 3000);
}
