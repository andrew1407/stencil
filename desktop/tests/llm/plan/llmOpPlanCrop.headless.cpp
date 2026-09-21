// The crop spec matrix: units, aspect in the spec and on the action, and the conflicts.
#include "llmOpPlanParts.hpp"

using namespace stencil::llm;

namespace llmopplan {

  void checkCropSpecs() {
  // ── crop ──
  std::printf("crop:\n");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
        "{\"x1\":\"10%\",\"x2\":\"-10%\",\"y1\":\"0\",\"y2\":\"9.5cm\"}}]}");
    check(r.ok && r.plan.actions.size() == 1, "crop with %/bare/cm tokens parses");
    check(r.plan.actions[0].x1 == "10%" && r.plan.actions[0].y2 == "9.5cm",
          "crop tokens preserved");
  }
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":{\"x1\":\"  10%  \"}}]}");
    check(r.ok && r.plan.actions[0].x1 == "10%", "crop tokens are trimmed before validation");
  }
  check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":{}}]}").ok,
        "empty crop spec fails");
  check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
                     "{\"z\":\"1\"}}]}")
             .ok,
        "unknown crop spec key fails");
  check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
                     "{\"x1\":\"10mm\"}}]}")
             .ok,
        "mm unit rejected (contract allows % px cm in)");
  check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
                     "{\"x1\":\"abc\"}}]}")
             .ok,
        "non-numeric crop token fails");
  check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
                     "{\"x1\":\"1px\"},\"extra\":1}]}")
             .ok,
        "unknown field on a crop action fails");
  {
    // The aspect key — strict W:H, digits only, both positive (browser
    // opPlan.test.js parity), passed through intact for core resolveCropRect.
    const auto r = parseOpPlan(
        "{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
        "{\"x1\":\"10%\",\"aspect\":\"4:3\"}}]}");
    check(r.ok && r.plan.actions.size() == 1, "crop with an aspect key parses");
    check(r.plan.actions[0].x1 == "10%" && r.plan.actions[0].aspect == "4:3",
          "edge token and aspect both preserved for the executor");
  }
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":{\"aspect\":\"1:1\"}}]}");
    check(r.ok && r.plan.actions[0].aspect == "1:1" && r.plan.actions[0].x1.isEmpty(),
          "aspect alone satisfies the at-least-one-key rule");
  }
  check(parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
                    "{\"aspect\":\"16:9\"}}]}")
            .ok,
        "16:9 aspect parses");
  for (const char* bad : {"0:3", "4:0", "-1:2", "4:-3", "3:4:5", "a:b", "1.5:2", "4",
                          "4:", ":3", "1e2:3", "", " 4:3 "}) {
    check(!parseOpPlan(QString("{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
                               "{\"aspect\":\"%1\"}}]}")
                           .arg(bad))
               .ok,
          qPrintable(QString("malformed aspect \"%1\" fails the whole plan").arg(bad)));
  }
  check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
                     "{\"aspect\":43}}]}")
             .ok,
        "non-string aspect fails");
  {
    // §3.2 action-level tolerance: "aspect" beside "spec" is accepted and
    // folds into the spec — same strict W:H rule as the canonical spelling.
    const auto r = parseOpPlan(
        "{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
        "{\"x1\":\"10%\"},\"aspect\":\"4:3\"}]}");
    check(r.ok && r.plan.actions.size() == 1 && r.plan.actions[0].x1 == "10%" &&
              r.plan.actions[0].aspect == "4:3",
          "action-level aspect folds into the spec");
  }
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":{},"
        "\"aspect\":\"1:1\"}]}");
    check(r.ok && r.plan.actions[0].aspect == "1:1" && r.plan.actions[0].x1.isEmpty(),
          "a folded action-level aspect satisfies the at-least-one-key rule");
  }
  check(!parseOpPlan(
             "{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"aspect\":\"1:1\"}]}")
             .ok,
        "action-level aspect never replaces the spec object itself");
  for (const char* bad : {"0:3", "4:0", "-1:2", "4:-3", "3:4:5", "a:b", "1.5:2", "4",
                          "4:", ":3", "1e2:3", "", " 4:3 "}) {
    check(!parseOpPlan(QString("{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
                               "{\"x1\":\"10%\"},\"aspect\":\"%1\"}]}")
                           .arg(bad))
               .ok,
          qPrintable(
              QString("malformed action-level aspect \"%1\" fails the whole plan").arg(bad)));
  }
  check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
                     "{\"x1\":\"10%\"},\"aspect\":43}]}")
             .ok,
        "non-string action-level aspect fails");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
        "{\"aspect\":\"4:3\"},\"aspect\":\"4:3\"}]}");
    check(r.ok && r.plan.actions[0].aspect == "4:3",
          "agreeing duplicate aspect keys are tolerated (spec kept)");
  }
  check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"crop\",\"spec\":"
                     "{\"aspect\":\"4:3\"},\"aspect\":\"16:9\"}]}")
             .ok,
        "conflicting duplicate aspect keys fail the whole plan");

  }

}  // namespace llmopplan
