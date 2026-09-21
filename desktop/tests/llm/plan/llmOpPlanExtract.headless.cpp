// Extraction tolerance and the top-level envelope: fences, the first balanced object, limits.
#include "llmOpPlanParts.hpp"

using namespace stencil::llm;

namespace llmopplan {

  void checkExtraction() {

  // ── extraction tolerance ──
  std::printf("extraction:\n");
  {
    const auto r = parseOpPlan("Just chatting, no JSON here.");
    check(r.ok && r.plan.chatOnly, "plain text -> chat-only, not an error");
    check(r.plan.reply == "Just chatting, no JSON here.", "chat-only reply is the raw text");
    check(r.plan.actions.isEmpty() && r.plan.variants.isEmpty(), "chat-only has no actions");
  }
  {
    const auto r = parseOpPlan(
        "```json\n{\"version\":1,\"reply\":\"ok\",\"actions\":[]}\n```");
    check(r.ok && !r.plan.chatOnly && r.plan.reply == "ok", "fenced JSON parses");
  }
  {
    const auto r = parseOpPlan(
        "Sure! {\"version\":1,\"reply\":\"done\",\"actions\":[{\"op\":\"rotate\","
        "\"dir\":\"left\"}]} hope that helps");
    check(r.ok && r.plan.actions.size() == 1, "first balanced object amid prose");
    check(r.plan.actions[0].op == OpKind::ROTATE && r.plan.actions[0].rotateLeft &&
              r.plan.actions[0].times == 1,
          "rotate defaults times=1");
  }
  {
    // Balanced braces that aren't JSON are skipped; the real object still found.
    const auto r = parseOpPlan("The set {a, b} maps to {\"reply\":\"found\"}");
    check(r.ok && !r.plan.chatOnly && r.plan.reply == "found",
          "non-JSON balanced braces skipped");
  }
  {
    // Braces inside the reply string don't break the brace counter.
    const auto r = parseOpPlan("{\"reply\":\"use {x1} tokens\",\"actions\":[]}");
    check(r.ok && r.plan.reply == "use {x1} tokens", "braces inside strings ignored");
  }
  {
    const auto r = parseOpPlan("{\"version\":2,\"reply\":\"ok\"}");
    check(r.ok && r.plan.reply == "ok", "version != 1 accepted but ignored");
  }

  // ── strict top-level validation ──
  std::printf("top-level:\n");
  {
    // §1 reply tolerance: "Done." + a warning, the plan itself survives.
    const auto r = parseOpPlan(
        "{\"actions\":[{\"op\":\"rotate\",\"dir\":\"left\"}]}");
    check(r.ok && r.plan.reply == "Done." && r.plan.actions.size() == 1 &&
              r.plan.warnings.size() == 1 &&
              r.plan.warnings[0].contains("omitted its reply"),
          "missing reply tolerated: Done. + warning, actions kept");
  }
  {
    // An EMPTY plan says so — a bare "Done." would read as a success that
    // never occurred (contract §1).
    const auto r = parseOpPlan("{\"reply\":\"   \"}");
    check(r.ok && r.plan.reply.contains("empty plan") && r.plan.warnings.isEmpty(),
          "blank reply on an empty plan: says nothing changed, no \"it ran\" claim");
  }
  {
    const auto r = parseOpPlan("{\"reply\":\"x\",\"actions\":{}}");
    check(!r.ok, "non-array actions fails the plan");
  }
  {
    QString many = "{\"reply\":\"x\",\"actions\":[";
    for (int i = 0; i < 17; ++i)
      many += QString("%1{\"op\":\"rotate\",\"dir\":\"left\"}").arg(i ? "," : "");
    many += "]}";
    check(!parseOpPlan(many).ok, "17 actions exceed the 16 limit");
  }
  check(!parseOpPlan("{\"reply\":\"x\",\"variants\":[{\"label\":7,\"actions\":[]}]}").ok,
        "a non-string variant label fails the plan (registry envelope)");
  check(parseOpPlan("{\"reply\":\"x\",\"variants\":[{\"label\":\"v\",\"actions\":[],\"note\":\"x\"}]}").ok,
        "a variant object tolerates undeclared keys (envelope allowUnknown)");
  {
    QString many = "{\"reply\":\"x\",\"variants\":[";
    for (int i = 0; i < 9; ++i) many += QString("%1{\"label\":\"v\"}").arg(i ? "," : "");
    many += "]}";
    check(!parseOpPlan(many).ok, "9 variants exceed the 8 limit");
  }
  {
    // Unknown op: dropped with a warning; the rest of the plan stands.
    const auto r = parseOpPlan(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"resize\",\"w\":10},"
        "{\"op\":\"rotate\",\"dir\":\"right\",\"times\":2}]}");
    check(r.ok, "unknown op does not fail the plan");
    check(r.plan.actions.size() == 1 && r.plan.actions[0].times == 2,
          "known action kept after unknown-op skip");
    check(r.plan.warnings.size() == 1 && r.plan.warnings[0].contains("resize"),
          "unknown op leaves a warning");
  }

  }

}  // namespace llmopplan
