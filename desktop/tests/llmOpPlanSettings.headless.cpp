// The §10 editor-settings ops, plus the fieldless clear and copy.
#include "llmOpPlanParts.hpp"

using namespace stencil::llm;

namespace llmopplan {

  void checkEditorSettings() {
  // ── §10 editor-settings ops (GUI editors) ──
  std::printf("editor settings (s10):\n");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"t\",\"actions\":[{\"op\":\"theme\",\"mode\":\"dark\"}]}");
    check(r.ok && r.plan.actions[0].op == OpKind::THEME &&
              r.plan.actions[0].mode == "dark",
          "theme dark parses");
  }
  check(!parseOpPlan("{\"reply\":\"t\",\"actions\":[{\"op\":\"theme\",\"mode\":\"blue\"}]}").ok,
        "theme mode must be light|dark");
  check(!parseOpPlan("{\"reply\":\"t\",\"actions\":[{\"op\":\"theme\",\"mode\":\"dark\","
                     "\"x\":1}]}")
             .ok,
        "unknown theme field fails");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"a\",\"actions\":[{\"op\":\"accent\",\"color\":\"#7c3aed\"}]}");
    check(r.ok && r.plan.actions[0].color == "#7c3aed", "accent hex parses");
  }
  check(!parseOpPlan("{\"reply\":\"a\",\"actions\":[{\"op\":\"accent\",\"color\":\"red\"}]}").ok,
        "accent requires #rrggbb (no names)");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"l\",\"actions\":[{\"op\":\"lineStyle\",\"color\":\"lime\","
        "\"thickness\":3,\"pointSize\":6,\"style\":\"dashed\"}]}");
    check(r.ok && r.plan.actions[0].color == "lime" && r.plan.actions[0].thickness == 3 &&
              r.plan.actions[0].pointSize == 6 && r.plan.actions[0].style == "dashed",
          "full lineStyle parses (CSS colour name ok)");
  }
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"l\",\"actions\":[{\"op\":\"lineStyle\",\"thickness\":20}]}");
    check(r.ok && r.plan.actions[0].thickness == 20 && r.plan.actions[0].color.isEmpty(),
          "single-field lineStyle subset parses");
  }
  check(!parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"lineStyle\"}]}").ok,
        "empty lineStyle fails (needs at least one field)");
  check(!parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"lineStyle\","
                     "\"thickness\":21}]}")
             .ok,
        "thickness 21 out of range (1..20)");
  check(!parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"lineStyle\","
                     "\"pointSize\":0}]}")
             .ok,
        "pointSize 0 out of range (1..30)");
  check(!parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"lineStyle\","
                     "\"style\":\"wavy\"}]}")
             .ok,
        "unknown lineStyle style fails");
  check(parseOpPlan("{\"reply\":\"u\",\"actions\":[{\"op\":\"units\",\"value\":\"in\"}]}").ok,
        "units in parses");
  check(!parseOpPlan("{\"reply\":\"u\",\"actions\":[{\"op\":\"units\",\"value\":\"px\"}]}").ok,
        "units px rejected (cm|in)");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"v\",\"actions\":[{\"op\":\"view\",\"points\":true}]}");
    check(r.ok && r.plan.actions[0].viewPoints == 1 && r.plan.actions[0].viewLines == -1,
          "view points-only parses (lines untouched)");
  }
  check(!parseOpPlan("{\"reply\":\"v\",\"actions\":[{\"op\":\"view\"}]}").ok,
        "empty view fails (needs at least one field)");
  check(!parseOpPlan("{\"reply\":\"v\",\"actions\":[{\"op\":\"view\",\"points\":1}]}").ok,
        "non-boolean view field fails");
  // §10 clear — "remove the image" means remove, not a white page from `blank`.
  {
    const auto r = parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"clear\"}]}");
    check(r.ok && r.plan.actions.size() == 1 && r.plan.actions[0].op == OpKind::CLEAR,
          "clear parses with no fields");
  }
  check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"clear\",\"color\":\"#fff\"}]}").ok,
        "clear takes no fields");
  check(variantDropped("{\"reply\":\"c\",\"actions\":[],\"variants\":[{\"label\":\"v\","
                       "\"actions\":[{\"op\":\"clear\"}]}]}"),
        "clear inside a variant drops it (variants exist to produce images)");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"c\",\"actions\":[{\"op\":\"connect\","
        "\"server\":\"stencil.example.com\"}]}");
    check(r.ok && r.plan.actions[0].op == OpKind::CONNECT &&
              r.plan.actions[0].server == "stencil.example.com",
          "connect server reference parses");
  }
  check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"connect\"}]}").ok,
        "connect without server fails");
  {
    // §10 openUrl: http(s) URL + optional incognito; junk fails; variant-banned.
    const auto r = parseOpPlan(
        "{\"reply\":\"o\",\"actions\":[{\"op\":\"openUrl\","
        "\"url\":\"https://a.example/cat.jpg\",\"incognito\":true}]}");
    check(r.ok && r.plan.actions[0].op == OpKind::OPEN_URL &&
              r.plan.actions[0].url == "https://a.example/cat.jpg" &&
              r.plan.actions[0].incognito,
          "openUrl parses with incognito");
    check(!parseOpPlan("{\"reply\":\"o\",\"actions\":[{\"op\":\"openUrl\"}]}").ok,
          "openUrl without url fails");
    check(!parseOpPlan("{\"reply\":\"o\",\"actions\":[{\"op\":\"openUrl\","
                       "\"url\":\"ftp://a.example/x\"}]}")
               .ok,
          "non-http(s) openUrl fails");
    check(!parseOpPlan("{\"reply\":\"o\",\"actions\":[{\"op\":\"openUrl\","
                       "\"url\":\"https://a.example/x\",\"tab\":1}]}")
               .ok,
          "unknown openUrl field fails");
    check(variantDropped("{\"reply\":\"o\",\"variants\":[{\"label\":\"v\",\"actions\":["
                         "{\"op\":\"openUrl\",\"url\":\"https://a.example/x\"}]}]}"),
          "openUrl inside a variant drops the variant");
  }
  // §10 copy — fieldless like clear; the working-image gate is the executor's.
  {
    const auto r = parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"copy\"}]}");
    check(r.ok && r.plan.actions.size() == 1 && r.plan.actions[0].op == OpKind::COPY,
          "copy parses with no fields");
  }
  check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"copy\",\"target\":\"x\"}]}").ok,
        "copy takes no fields");
  check(variantDropped("{\"reply\":\"c\",\"variants\":[{\"label\":\"v\","
                       "\"actions\":[{\"op\":\"copy\"}]}]}"),
        "copy inside a variant drops it (variants exist to produce images)");
  // Ask-option previews are rendered, never executed: same scope, same drop.
  check(previewDropped("{\"reply\":\"c\",\"ask\":{\"question\":\"q?\",\"options\":["
                       "{\"label\":\"a\",\"actions\":[{\"op\":\"copy\"}]},{\"label\":\"b\"}]}}"),
        "copy inside an ask-option preview drops the preview");

  }

}  // namespace llmopplan
