// The §10 editor-settings rows added since: each one's own field matrix.
#include "llmOpPlanParts.hpp"

using namespace stencil::llm;

namespace llmopplan {

  void checkEditorRows() {
  // §10 new rows: compare / zoom / renameProject / projectColor / blankColor /
  // openProject / incognito.
  std::printf("editor settings new rows (s10):\n");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"v\",\"actions\":[{\"op\":\"compare\",\"mode\":\"vertical\","
        "\"split\":0.25}]}");
    check(r.ok && r.plan.actions[0].op == OpKind::COMPARE &&
              r.plan.actions[0].mode == "vertical" && r.plan.actions[0].split == 0.25,
          "compare with split parses");
    check(parseOpPlan("{\"reply\":\"v\",\"actions\":[{\"op\":\"compare\","
                      "\"mode\":\"none\"}]}").ok,
          "compare none parses");
    check(!parseOpPlan("{\"reply\":\"v\",\"actions\":[{\"op\":\"compare\","
                       "\"mode\":\"sideways\"}]}").ok,
          "unknown compare mode fails");
    // The divider belongs to the SPLIT modes only (registry onlyWith; fixture 160):
    // echoed beside "none"/"original" it fails the plan like any misplaced field.
    check(!parseOpPlan("{\"reply\":\"v\",\"actions\":[{\"op\":\"compare\","
                       "\"mode\":\"none\",\"split\":0.5}]}").ok,
          "split with mode none fails (split modes only)");
    check(!parseOpPlan("{\"reply\":\"v\",\"actions\":[{\"op\":\"compare\","
                       "\"mode\":\"original\",\"split\":0.5}]}").ok,
          "split with mode original fails too");
    check(!parseOpPlan("{\"reply\":\"v\",\"actions\":[{\"op\":\"compare\","
                       "\"mode\":\"vertical\",\"split\":0.01}]}").ok,
          "split below 0.02 fails");
    check(!parseOpPlan("{\"reply\":\"v\",\"actions\":[{\"op\":\"compare\","
                       "\"mode\":\"horizontal\",\"split\":0.99}]}").ok,
          "split above 0.98 fails");
  }
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"z\",\"actions\":[{\"op\":\"zoom\",\"percent\":150}]}");
    check(r.ok && r.plan.actions[0].op == OpKind::ZOOM && r.plan.actions[0].percent == 150,
          "zoom percent parses");
    const auto f = parseOpPlan("{\"reply\":\"z\",\"actions\":[{\"op\":\"zoom\",\"fit\":true}]}");
    check(f.ok && f.plan.actions[0].fit, "zoom fit:true parses");
    check(!parseOpPlan("{\"reply\":\"z\",\"actions\":[{\"op\":\"zoom\"}]}").ok,
          "zoom with neither form fails");
    check(!parseOpPlan("{\"reply\":\"z\",\"actions\":[{\"op\":\"zoom\","
                       "\"percent\":150,\"fit\":true}]}").ok,
          "zoom with both forms fails");
    check(!parseOpPlan("{\"reply\":\"z\",\"actions\":[{\"op\":\"zoom\",\"percent\":4}]}").ok,
          "percent below 5 fails");
    check(!parseOpPlan("{\"reply\":\"z\",\"actions\":[{\"op\":\"zoom\","
                       "\"percent\":3201}]}").ok,
          "percent above 3200 fails");
    check(!parseOpPlan("{\"reply\":\"z\",\"actions\":[{\"op\":\"zoom\",\"fit\":false}]}").ok,
          "fit:false fails (true is the only value)");
  }
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"r\",\"actions\":[{\"op\":\"renameProject\",\"name\":\" new name \"}]}");
    check(r.ok && r.plan.actions[0].op == OpKind::RENAME_PROJECT &&
              r.plan.actions[0].name == "new name",
          "renameProject parses (trimmed)");
    check(!parseOpPlan(QStringLiteral("{\"reply\":\"r\",\"actions\":[{\"op\":\"renameProject\","
                                      "\"name\":\"%1\"}]}")
                           .arg(QString(81, QLatin1Char('a')))).ok,
          "a renameProject name over 80 chars fails");
    check(!parseOpPlan("{\"reply\":\"r\",\"actions\":[{\"op\":\"renameProject\"}]}").ok,
          "renameProject without a name fails");
  }
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"c\",\"actions\":[{\"op\":\"projectColor\",\"color\":\"#ec4899\"}]}");
    check(r.ok && r.plan.actions[0].op == OpKind::PROJECT_COLOR &&
              r.plan.actions[0].color == "#ec4899",
          "projectColor hex parses");
    const auto clear = parseOpPlan(
        "{\"reply\":\"c\",\"actions\":[{\"op\":\"projectColor\",\"color\":\"\"}]}");
    check(clear.ok && clear.plan.actions[0].color.isEmpty(),
          "projectColor \"\" parses as the explicit clear");
    check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"projectColor\","
                       "\"color\":\"pink\"}]}").ok,
          "projectColor rejects colour names");
  }
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"b\",\"actions\":[{\"op\":\"blankColor\",\"color\":\"lightblue\"}]}");
    check(r.ok && r.plan.actions[0].op == OpKind::BLANK_COLOR &&
              r.plan.actions[0].color == "lightblue",
          "blankColor CSS name parses");
    check(!parseOpPlan("{\"reply\":\"b\",\"actions\":[{\"op\":\"blankColor\","
                       "\"color\":\"notacolor\"}]}").ok,
          "unknown blankColor name fails");
    check(!parseOpPlan("{\"reply\":\"b\",\"actions\":[{\"op\":\"blankColor\"}]}").ok,
          "blankColor without a colour fails");
  }
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"o\",\"actions\":[{\"op\":\"openProject\",\"name\":\"portrait 1\"}]}");
    check(r.ok && r.plan.actions[0].op == OpKind::OPEN_PROJECT &&
              r.plan.actions[0].name == "portrait 1",
          "openProject parses");
    check(!parseOpPlan(QStringLiteral("{\"reply\":\"o\",\"actions\":[{\"op\":\"openProject\","
                                      "\"name\":\"%1\"}]}")
                           .arg(QString(121, QLatin1Char('a')))).ok,
          "an openProject name over 120 chars fails");
  }
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"i\",\"actions\":[{\"op\":\"incognito\",\"on\":true}]}");
    check(r.ok && r.plan.actions[0].op == OpKind::INCOGNITO && r.plan.actions[0].incognito,
          "incognito on:true parses");
    const auto off = parseOpPlan(
        "{\"reply\":\"i\",\"actions\":[{\"op\":\"incognito\",\"on\":false}]}");
    check(off.ok && !off.plan.actions[0].incognito, "incognito on:false parses");
    check(!parseOpPlan("{\"reply\":\"i\",\"actions\":[{\"op\":\"incognito\"}]}").ok,
          "incognito without on fails");
    check(!parseOpPlan("{\"reply\":\"i\",\"actions\":[{\"op\":\"incognito\","
                       "\"on\":\"yes\"}]}").ok,
          "non-boolean on fails");
  }
  {
    // Every new row is an editor-settings op: variant/ask-preview banned —
    // §1 drops the variant carrying one, keeping the plan alive.
    const char* banned[] = {
        "{\"reply\":\"v\",\"variants\":[{\"actions\":[{\"op\":\"compare\",\"mode\":\"none\"}]}]}",
        "{\"reply\":\"v\",\"variants\":[{\"actions\":[{\"op\":\"zoom\",\"fit\":true}]}]}",
        "{\"reply\":\"v\",\"variants\":[{\"actions\":[{\"op\":\"renameProject\",\"name\":\"x\"}]}]}",
        "{\"reply\":\"v\",\"variants\":[{\"actions\":[{\"op\":\"projectColor\",\"color\":\"\"}]}]}",
        "{\"reply\":\"v\",\"variants\":[{\"actions\":[{\"op\":\"blankColor\",\"color\":\"red\"}]}]}",
        "{\"reply\":\"v\",\"variants\":[{\"actions\":[{\"op\":\"openProject\",\"name\":\"x\"}]}]}",
        "{\"reply\":\"v\",\"variants\":[{\"actions\":[{\"op\":\"incognito\",\"on\":true}]}]}",
    };
    bool allDropped = true;
    for (const char* json : banned) {
      const auto r = parseOpPlan(QString::fromUtf8(json));
      if (!r.ok || !r.plan.variants.isEmpty() || r.plan.warnings.size() != 1)
        allDropped = false;
    }
    check(allDropped, "every new editor row drops its variant with a warning");
    check(isEditorSettingsOp(OpKind::COMPARE) && isEditorSettingsOp(OpKind::INCOGNITO),
          "the new rows carry the editor-settings property");
  }
  check(!parseOpPlan("{\"reply\":\"c\",\"actions\":[{\"op\":\"disconnect\","
                     "\"server\":\"  \"}]}")
             .ok,
        "blank disconnect server fails");
  {
    // Variants ban: a §10 op inside a variant costs THAT variant, not the plan.
    const auto r = parseOpPlan(
        "{\"reply\":\"v\",\"variants\":[{\"label\":\"t\",\"actions\":["
        "{\"op\":\"theme\",\"mode\":\"dark\"}]}]}");
    check(r.ok && r.plan.variants.isEmpty(),
          "editor-settings op inside a variant drops the variant");
    check(r.plan.warnings.size() == 1 &&
              r.plan.warnings[0].contains("Dropped variant \"t\"") &&
              r.plan.warnings[0].contains("\"theme\" is an editor-settings op"),
          "…with a warning naming the variant and the op");
  }
  {
    // An unlabelled variant is named by its 1-based position instead.
    const auto r = parseOpPlan("{\"reply\":\"v\",\"variants\":[{\"actions\":["
                               "{\"op\":\"connect\",\"server\":\"a.example.com\"}]}]}");
    check(r.ok && r.plan.variants.isEmpty() && r.plan.warnings.size() == 1 &&
              r.plan.warnings[0].contains("Dropped variant 1"),
          "connect inside a variant drops it, named by position");
  }
  {
    // openUrl gets a steer, not just a drop: top-level, or the extension.
    const auto r = parseOpPlan(
        "{\"reply\":\"v\",\"variants\":[{\"actions\":["
        "{\"op\":\"openUrl\",\"url\":\"https://a.example/x.jpg\"}]}]}");
    check(r.ok && r.plan.variants.isEmpty() && r.plan.warnings.size() == 1 &&
              r.plan.warnings[0].contains("top-level action") &&
              r.plan.warnings[0].contains("extension assistant"),
          "openUrl inside a variant is dropped with the extension hint");
  }

  }

}  // namespace llmopplan
