// Several images in one plan (§2.1), and the variant envelope.
#include "llmOpPlanParts.hpp"

using namespace stencil::llm;

namespace llmopplan {

  void checkMultiImageAndVariants() {
  // ── §2.1 multi-image ops: `image` switches to a turn attachment, `save` persists ──
  std::printf("multi-image ops (s2.1):\n");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"both\",\"actions\":[{\"op\":\"image\",\"index\":2},"
        "{\"op\":\"save\",\"name\":\"portrait 1\"},{\"op\":\"save\"}]}");
    check(r.ok && r.plan.actions.size() == 3, "image + save parse");
    check(r.plan.actions[0].op == OpKind::IMAGE && r.plan.actions[0].index == 2,
          "image keeps its 1-based index");
    check(r.plan.actions[1].op == OpKind::SAVE && r.plan.actions[1].name == "portrait 1",
          "save keeps its name");
    check(r.plan.actions[2].op == OpKind::SAVE && r.plan.actions[2].name.isEmpty(),
          "a nameless save is allowed (the name is derived at execution)");
  }
  {
    // 1-based: 0, negatives and non-integers are not an attachment.
    const char* bad[] = {
        "{\"reply\":\"x\",\"actions\":[{\"op\":\"image\",\"index\":0}]}",
        "{\"reply\":\"x\",\"actions\":[{\"op\":\"image\",\"index\":-1}]}",
        "{\"reply\":\"x\",\"actions\":[{\"op\":\"image\",\"index\":1.5}]}",
        "{\"reply\":\"x\",\"actions\":[{\"op\":\"image\",\"index\":\"1\"}]}",
        "{\"reply\":\"x\",\"actions\":[{\"op\":\"image\"}]}",
        "{\"reply\":\"x\",\"actions\":[{\"op\":\"image\",\"index\":1,\"sneaky\":2}]}",
    };
    bool allRejected = true;
    for (const char* json : bad)
      if (parseOpPlan(QString::fromUtf8(json)).ok) allRejected = false;
    check(allRejected, "every bad image index rejects the plan");
  }
  {
    const QString longName = QString(121, QLatin1Char('x'));
    const auto r = parseOpPlan(
        QStringLiteral("{\"reply\":\"x\",\"actions\":[{\"op\":\"save\",\"name\":\"%1\"}]}")
            .arg(longName));
    check(!r.ok && r.error.contains("120"), "a save name over 120 chars is rejected");
    check(!parseOpPlan("{\"reply\":\"x\",\"actions\":[{\"op\":\"save\",\"name\":5}]}").ok,
          "a non-string save name is rejected");
    check(!parseOpPlan("{\"reply\":\"x\",\"actions\":[{\"op\":\"save\",\"as\":\"a\"}]}").ok,
          "an unknown save field is rejected");
    check(parseOpPlan(
              QStringLiteral("{\"reply\":\"x\",\"actions\":[{\"op\":\"save\",\"name\":\"%1\"}]}")
                  .arg(QString(120, QLatin1Char('x'))))
              .ok,
          "exactly 120 chars still fits");
  }
  {
    // Top-level only: neither may hide inside a variant or an ask-option preview
    // — §1 drops the variant/preview that carries one, never the plan.
    const auto v1 = parseOpPlan(
        "{\"reply\":\"v\",\"variants\":[{\"label\":\"a\",\"actions\":["
        "{\"op\":\"image\",\"index\":1}]}]}");
    check(v1.ok && v1.plan.variants.isEmpty() && v1.plan.warnings.size() == 1 &&
              v1.plan.warnings[0].contains("Dropped variant \"a\"") &&
              v1.plan.warnings[0].contains("top-level") &&
              v1.plan.warnings[0].contains("2.1"),
          "image inside a variant drops the variant");
    const auto v2 = parseOpPlan(
        "{\"reply\":\"v\",\"variants\":[{\"label\":\"a\",\"actions\":[{\"op\":\"save\"}]}]}");
    check(v2.ok && v2.plan.variants.isEmpty() &&
              v2.plan.warnings.value(0).contains("top-level"),
          "save inside a variant drops the variant");
    const auto a1 = parseOpPlan(
        "{\"reply\":\"x\",\"ask\":{\"question\":\"Q\",\"options\":["
        "{\"label\":\"A\",\"actions\":[{\"op\":\"save\"}]},{\"label\":\"B\"}]}}");
    check(a1.ok && a1.plan.ask.options.size() == 2 &&
              a1.plan.ask.options[0].actions.isEmpty() &&
              a1.plan.warnings.value(0).contains("top-level"),
          "save inside an ask-option preview drops the preview only");
    const auto a2 = parseOpPlan(
        "{\"reply\":\"x\",\"ask\":{\"question\":\"Q\",\"options\":["
        "{\"label\":\"A\",\"actions\":[{\"op\":\"image\",\"index\":1}]},{\"label\":\"B\"}]}}");
    check(a2.ok && a2.plan.ask.options.size() == 2 &&
              a2.plan.ask.options[0].actions.isEmpty(),
          "image inside an ask-option preview drops the preview only");
  }
  {
    // The two enum predicates stay apart: §2.1 ops are top-level only WITHOUT
    // being editor-settings ops (the §10 variant message is not theirs).
    check(isTopLevelOnlyOp(OpKind::IMAGE) && isTopLevelOnlyOp(OpKind::SAVE),
          "image/save are top-level only");
    check(!isEditorSettingsOp(OpKind::IMAGE) && !isEditorSettingsOp(OpKind::SAVE),
          "…but they are not editor-settings ops");
    check(isEditorSettingsOp(OpKind::THEME) && isEditorSettingsOp(OpKind::DISCONNECT) &&
              isTopLevelOnlyOp(OpKind::OPEN_URL),
          "the §10 ops keep both properties");
  }

  // ── variants + labels ──
  std::printf("variants:\n");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"v\",\"actions\":[],\"variants\":["
        "{\"label\":\"rotated\",\"actions\":[{\"op\":\"rotate\",\"dir\":\"right\"}]},"
        "{\"actions\":[{\"op\":\"filter\",\"mode\":\"sepia\"}]}]}");
    check(r.ok && r.plan.variants.size() == 2, "two variants parse");
    check(r.plan.variants[0].label == "rotated" &&
              r.plan.variants[0].actions.size() == 1,
          "variant label + actions kept");
    check(r.plan.variants[1].label.isEmpty(), "label-less variant allowed");
  }
  {
    // A KNOWN op with bad params inside a variant fails the whole plan.
    const auto r = parseOpPlan(
        "{\"reply\":\"v\",\"variants\":[{\"label\":\"x\",\"actions\":["
        "{\"op\":\"rotate\",\"dir\":\"sideways\"}]}]}");
    check(!r.ok, "invalid known op in a variant fails the plan");
  }
  {
    // §1's one exception (the reported bug): one misplaced op used to destroy
    // the whole turn. The bad variant goes; the actions and the good variant run.
    const auto r = parseOpPlan(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"rotate\",\"dir\":\"left\"}],\"variants\":["
        "{\"label\":\"wiped\",\"actions\":[{\"op\":\"clear\"}]},"
        "{\"label\":\"sepia\",\"actions\":[{\"op\":\"filter\",\"mode\":\"sepia\"}]}]}");
    check(r.ok && r.error.isEmpty(), "a misplaced settings op no longer fails the plan");
    check(r.plan.actions.size() == 1 && r.plan.actions[0].op == OpKind::ROTATE,
          "the top-level actions survive");
    check(r.plan.variants.size() == 1 && r.plan.variants[0].label == "sepia",
          "the well-formed variant survives");
    check(r.plan.warnings.size() == 1 &&
              r.plan.warnings[0].contains("Dropped variant \"wiped\"") &&
              r.plan.warnings[0].contains("\"clear\" is an editor-settings op") &&
              r.plan.warnings[0].contains("top-level actions"),
          "the warning names the dropped variant, the op, and where it belongs");
  }
  {
    // A plan made ONLY of such a variant: reply + warning, nothing to execute.
    const auto r = parseOpPlan(
        "{\"reply\":\"here you go\",\"variants\":[{\"label\":\"v\",\"actions\":["
        "{\"op\":\"clear\"}]}]}");
    check(r.ok && r.plan.reply == "here you go", "…and the reply still stands");
    check(r.plan.actions.isEmpty() && r.plan.variants.isEmpty() &&
              r.plan.warnings.size() == 1,
          "nothing executes, one warning explains why");
  }
  {
    // A dropped variant takes its OWN warnings with it — a render that never
    // happens has nothing to report.
    const auto r = parseOpPlan(
        "{\"reply\":\"v\",\"variants\":[{\"label\":\"v\",\"actions\":["
        "{\"op\":\"wobble\"},{\"op\":\"save\"}]}]}");
    check(r.ok && r.plan.warnings.size() == 1 &&
              r.plan.warnings[0].contains("Dropped variant"),
          "the dropped variant's unknown-op warning goes with it");
  }
  check(sanitizeLabel("  Sepia / warm! <v1>  ") == "Sepia warm v1", "labels sanitized");
  check(sanitizeLabel("###") .isEmpty(), "all-symbol label sanitizes to empty");

  }

}  // namespace llmopplan
