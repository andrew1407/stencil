// §10 project management, and the later removeProject/copy/accent/lineStyle forms.
#include "llmOpPlanParts.hpp"

using namespace stencil::llm;

namespace llmopplan {

  void checkProjectOps() {
  // §10 project management — removeProject/clearProjects shapes; both are
  // editor-settings scoped, so the variant/ask-preview ban applies.
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"p\",\"actions\":[{\"op\":\"removeProject\",\"name\":\" portrait 1 \"},"
        "{\"op\":\"clearProjects\"}]}");
    check(r.ok && r.plan.actions.size() == 2 &&
              r.plan.actions[0].op == OpKind::REMOVE_PROJECT &&
              r.plan.actions[0].name == "portrait 1" &&
              r.plan.actions[1].op == OpKind::CLEAR_PROJECTS,
          "removeProject (name trimmed) + clearProjects parse");
  }
  check(!parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"removeProject\"}]}").ok,
        "removeProject without a name fails");
  check(!parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"removeProject\","
                     "\"name\":\"   \"}]}").ok,
        "blank removeProject name fails");
  check(!parseOpPlan(QStringLiteral("{\"reply\":\"p\",\"actions\":[{\"op\":\"removeProject\","
                                    "\"name\":\"%1\"}]}")
                         .arg(QString(121, QLatin1Char('a')))).ok,
        "a removeProject name over 120 chars fails");
  check(!parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"removeProject\","
                     "\"name\":\"x\",\"id\":\"y\"}]}").ok,
        "unknown removeProject field fails");
  check(!parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"clearProjects\","
                     "\"name\":\"x\"}]}").ok,
        "clearProjects takes no fields");
  check(variantDropped("{\"reply\":\"p\",\"variants\":[{\"label\":\"v\",\"actions\":["
                       "{\"op\":\"removeProject\",\"name\":\"x\"}]}]}"),
        "removeProject inside a variant drops it");
  check(variantDropped("{\"reply\":\"p\",\"variants\":[{\"label\":\"v\",\"actions\":["
                       "{\"op\":\"clearProjects\"}]}]}"),
        "clearProjects inside a variant drops it");
  check(previewDropped(
            "{\"reply\":\"p\",\"ask\":{\"question\":\"q?\",\"options\":["
            "{\"label\":\"a\",\"actions\":[{\"op\":\"clearProjects\"}]},{\"label\":\"b\"}]}}"),
        "clearProjects inside an ask-option preview drops the preview");
  // §10 removeProject's `current: true` form (exactly one of name/current).
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"p\",\"actions\":[{\"op\":\"removeProject\",\"current\":true}]}");
    check(r.ok && r.plan.actions[0].op == OpKind::REMOVE_PROJECT &&
              r.plan.actions[0].current && r.plan.actions[0].name.isEmpty(),
          "removeProject current:true parses");
    check(!parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"removeProject\","
                       "\"current\":false}]}").ok,
          "removeProject current:false fails (true is the only value)");
    check(!parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"removeProject\","
                       "\"name\":\"x\",\"current\":true}]}").ok,
          "removeProject with both name and current fails");
  }
  // §10 copy's `what` form.
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"c\",\"actions\":[{\"op\":\"copy\",\"what\":\"layout\"}]}");
    check(r.ok && r.plan.actions[0].op == OpKind::COPY && r.plan.actions[0].what == "layout",
          "copy what:layout parses");
    check(parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"copy\","
                      "\"what\":\"image\"}]}").ok,
          "copy what:image parses");
    check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"copy\","
                       "\"what\":\"lines\"}]}").ok,
          "copy what must be image|layout");
  }
  // §10 accent's `preset` form (exactly one of color/preset).
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"a\",\"actions\":[{\"op\":\"accent\",\"preset\":\"green\"}]}");
    check(r.ok && r.plan.actions[0].preset == "green" && r.plan.actions[0].color.isEmpty(),
          "accent preset parses");
    check(!parseOpPlan("{\"reply\":\"a\",\"actions\":[{\"op\":\"accent\"}]}").ok,
          "accent with neither form fails");
    check(!parseOpPlan("{\"reply\":\"a\",\"actions\":[{\"op\":\"accent\","
                       "\"color\":\"#112233\",\"preset\":\"green\"}]}").ok,
          "accent with both forms fails");
    check(!parseOpPlan("{\"reply\":\"a\",\"actions\":[{\"op\":\"accent\","
                       "\"preset\":\"  \"}]}").ok,
          "blank accent preset fails");
  }
  // §10 lineStyle widening: pointColor ("" = follow stroke), drawMode, fillColor.
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"l\",\"actions\":[{\"op\":\"lineStyle\",\"pointColor\":\"\","
        "\"drawMode\":\"rect\",\"fillColor\":\"transparent\"}]}");
    check(r.ok && r.plan.actions[0].pointColorSet && r.plan.actions[0].pointColor.isEmpty(),
          "pointColor \"\" parses as an explicit follow-the-stroke");
    check(r.plan.actions[0].drawMode == "rect" &&
              r.plan.actions[0].fillColor == "transparent",
          "drawMode + fillColor kept");
    check(parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"lineStyle\","
                      "\"pointColor\":\"#A1B2C3\"}]}").ok,
          "hex pointColor parses (and satisfies the at-least-one rule)");
    check(parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"lineStyle\","
                      "\"fillColor\":\"#112233\"}]}").ok,
          "hex fillColor parses (skip-note is the executor's)");
    check(!parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"lineStyle\","
                       "\"pointColor\":\"red\"}]}").ok,
          "pointColor rejects colour names (hex or \"\" only)");
    check(!parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"lineStyle\","
                       "\"drawMode\":\"circle\"}]}").ok,
          "drawMode must be line|rect");
    check(!parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"lineStyle\","
                       "\"fillColor\":\"none\"}]}").ok,
          "fillColor must be #rrggbb or transparent");
  }

  }

}  // namespace llmopplan
