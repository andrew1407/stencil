// Page and blank formats, undo/redo step ranges, and the frame indices.
#include "llmOpPlanParts.hpp"

using namespace stencil::llm;

namespace llmopplan {

  void checkPageAndHistory() {
  // ── page / blank ──
  std::printf("page/blank:\n");
  check(parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"page\",\"format\":\"a4\"}]}").ok,
        "page a4 parses");
  check(parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"page\",\"format\":\"c10\"}]}").ok,
        "page c10 parses");
  check(!parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"page\",\"format\":\"A4\"}]}").ok,
        "uppercase format rejected (contract: lowercase)");
  check(!parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"page\",\"format\":\"a11\"}]}").ok,
        "a11 out of range");
  check(!parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"page\",\"format\":\"d4\"}]}").ok,
        "d series rejected");
  check(parseOpPlan("{\"reply\":\"b\",\"actions\":[{\"op\":\"blank\",\"color\":\"#ffffff\","
                    "\"format\":\"a4\"}]}")
            .ok,
        "blank with hex + format parses");
  check(parseOpPlan("{\"reply\":\"b\",\"actions\":[{\"op\":\"blank\",\"color\":\"red\"}]}").ok,
        "blank with CSS colour name parses");
  check(!parseOpPlan("{\"reply\":\"b\",\"actions\":[{\"op\":\"blank\",\"color\":\"#fff\"}]}").ok,
        "#fff shorthand rejected (contract: #rrggbb)");
  check(!parseOpPlan("{\"reply\":\"b\",\"actions\":[{\"op\":\"blank\","
                     "\"color\":\"notacolor\"}]}")
             .ok,
        "unknown colour name fails");
  {
    // §2: page takes format OR custom cm dims — exactly one form.
    const auto r = parseOpPlan(
        "{\"reply\":\"p\",\"actions\":[{\"op\":\"page\",\"width\":20,\"height\":30}]}");
    check(r.ok && r.plan.actions[0].widthCm == 20.0 && r.plan.actions[0].heightCm == 30.0,
          "custom page dims parse (cm)");
    check(!parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"page\"}]}").ok,
          "page with neither form fails");
    check(!parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"page\","
                       "\"format\":\"a4\",\"width\":20,\"height\":30}]}")
               .ok,
          "page with both forms fails");
    check(!parseOpPlan("{\"reply\":\"p\",\"actions\":[{\"op\":\"page\",\"width\":20}]}").ok,
          "page width without height fails");
    for (const char* bad : {"0.05", "501", "-3", "0"}) {
      check(!parseOpPlan(QString("{\"reply\":\"p\",\"actions\":[{\"op\":\"page\","
                                 "\"width\":%1,\"height\":10}]}")
                             .arg(bad))
                 .ok,
            qPrintable(QString("page dim %1 out of 0.1..500 fails").arg(bad)));
    }
  }
  {
    // §2: blank's optional cm dims — both or neither, overriding format.
    const auto r = parseOpPlan(
        "{\"reply\":\"b\",\"actions\":[{\"op\":\"blank\",\"color\":\"#ffffff\","
        "\"format\":\"a4\",\"width\":10,\"height\":5}]}");
    check(r.ok && r.plan.actions[0].widthCm == 10.0 && r.plan.actions[0].heightCm == 5.0 &&
              r.plan.actions[0].format == "a4",
          "blank dims ride beside the format (dims win at execution)");
    check(!parseOpPlan("{\"reply\":\"b\",\"actions\":[{\"op\":\"blank\","
                       "\"color\":\"#ffffff\",\"width\":10}]}")
               .ok,
          "blank width without height fails");
    check(!parseOpPlan("{\"reply\":\"b\",\"actions\":[{\"op\":\"blank\","
                       "\"color\":\"#ffffff\",\"width\":10,\"height\":900}]}")
               .ok,
          "blank dim out of 0.1..500 fails");
  }

  // ── §2 undo / redo ──
  std::printf("undo/redo (s2):\n");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"u\",\"actions\":[{\"op\":\"undo\"},{\"op\":\"redo\",\"steps\":20}]}");
    check(r.ok && r.plan.actions.size() == 2, "undo + redo parse");
    check(r.plan.actions[0].op == OpKind::UNDO && r.plan.actions[0].steps == 1,
          "undo defaults steps=1");
    check(r.plan.actions[1].op == OpKind::REDO && r.plan.actions[1].steps == 20,
          "redo keeps its steps");
  }
  check(!parseOpPlan("{\"reply\":\"u\",\"actions\":[{\"op\":\"undo\",\"steps\":0}]}").ok,
        "undo steps 0 out of range (1..20)");
  check(!parseOpPlan("{\"reply\":\"u\",\"actions\":[{\"op\":\"redo\",\"steps\":21}]}").ok,
        "redo steps 21 out of range");
  check(!parseOpPlan("{\"reply\":\"u\",\"actions\":[{\"op\":\"undo\",\"all\":true}]}").ok,
        "unknown undo field fails");
  {
    // Top-level only: history is invisible inside a sandboxed variant/preview —
    // §1 drops that variant/preview with a warning, never the whole plan.
    const auto v = parseOpPlan(
        "{\"reply\":\"u\",\"variants\":[{\"label\":\"v\",\"actions\":[{\"op\":\"undo\"}]}]}");
    check(v.ok && v.plan.variants.isEmpty(), "undo inside a variant drops the variant");
    check(v.plan.warnings.size() == 1 &&
              v.plan.warnings[0].contains("Dropped variant \"v\"") &&
              v.plan.warnings[0].contains("history"),
          "…named, with its own history wording");
    const auto p = parseOpPlan(
        "{\"reply\":\"u\",\"ask\":{\"question\":\"Q\",\"options\":["
        "{\"label\":\"A\",\"actions\":[{\"op\":\"redo\"}]},{\"label\":\"B\"}]}}");
    check(p.ok && p.plan.ask.options.size() == 2 && p.plan.ask.options[0].actions.isEmpty(),
          "redo inside an ask-option preview drops the preview, keeps the option");
    check(isTopLevelOnlyOp(OpKind::UNDO) && isTopLevelOnlyOp(OpKind::REDO) &&
              !isEditorSettingsOp(OpKind::UNDO) && !isEditorSettingsOp(OpKind::REDO),
          "undo/redo are top-level only without being editor-settings ops");
  }
  {
    // §2 reset: editors have no single reset control — the op stays UNKNOWN
    // here and is skipped with a warning per §1.
    const auto r = parseOpPlan(
        "{\"reply\":\"r\",\"actions\":[{\"op\":\"reset\"},"
        "{\"op\":\"rotate\",\"dir\":\"left\"}]}");
    check(r.ok && r.plan.actions.size() == 1 && r.plan.actions[0].op == OpKind::ROTATE,
          "reset is dropped as an unknown op; the rest of the plan stands");
    check(r.plan.warnings.size() == 1 && r.plan.warnings[0].contains("reset"),
          "…with the unknown-op warning naming it");
  }

  // ── frame ──
  std::printf("frame:\n");
  {
    const auto r =
        parseOpPlan("{\"reply\":\"v\",\"actions\":[{\"op\":\"frame\",\"index\":7}]}");
    check(r.ok && r.plan.actions[0].indices == QVector<int>{7}, "frame index parses");
  }
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"v\",\"actions\":[{\"op\":\"frame\",\"indices\":[0,30,60]}]}");
    check(r.ok && r.plan.actions[0].indices == (QVector<int>{0, 30, 60}),
          "frame indices parse");
  }
  check(!parseOpPlan("{\"reply\":\"v\",\"actions\":[{\"op\":\"frame\",\"index\":1,"
                     "\"indices\":[2]}]}")
             .ok,
        "both index and indices fails");
  check(!parseOpPlan("{\"reply\":\"v\",\"actions\":[{\"op\":\"frame\"}]}").ok,
        "neither index nor indices fails");
  check(!parseOpPlan("{\"reply\":\"v\",\"actions\":[{\"op\":\"frame\",\"index\":-1}]}").ok,
        "negative index fails");
  {
    QString idx;
    for (int i = 0; i < 33; ++i) idx += QString("%1%2").arg(i ? "," : "").arg(i);
    check(!parseOpPlan("{\"reply\":\"v\",\"actions\":[{\"op\":\"frame\",\"indices\":[" +
                       idx + "]}]}")
               .ok,
          "33 indices exceed the 32 limit");
  }

  }

}  // namespace llmopplan
