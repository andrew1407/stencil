// The ask card (§11): its question, options and the previews they carry.
#include "llmOpPlanParts.hpp"

using namespace stencil::llm;

namespace llmopplan {

  void checkAskCards() {
  // ── §11 interactive replies (`ask`) ──
  std::printf("ask (contract 11):\n");
  {
    const auto r = parseOpPlan(
        R"({"version":1,"reply":"pick","ask":{"question":"Which tint?","options":[{"label":"Sepia"},{"label":"B&W"}]}})");
    check(r.ok, "a card parses");
    check(r.plan.ask.question == "Which tint?", "question kept");
    check(!r.plan.ask.multi, "single is the default mode");
    check(!r.plan.ask.allowCustom, "allowCustom defaults off");
    check(r.plan.ask.options.size() == 2 && r.plan.ask.options[0].label == "Sepia", "options kept in order");
  }
  {
    const auto r = parseOpPlan(
        R"({"version":1,"reply":"pick","ask":{"question":"  Which?  ","mode":"multi","allowCustom":true,"customLabel":" Other ","options":[{"label":"  A  "},{"label":"B"}]}})");
    check(r.ok && r.plan.ask.multi, "multi mode");
    check(r.plan.ask.allowCustom && r.plan.ask.customLabel == "Other", "custom row, trimmed");
    check(r.plan.ask.question == "Which?" && r.plan.ask.options[0].label == "A", "strings trimmed");
  }
  {
    const auto r = parseOpPlan(R"({"version":1,"reply":"hi","actions":[]})");
    check(r.ok && r.plan.ask.options.isEmpty(), "no card on an ordinary turn");
    const auto c = parseOpPlan("just chatting");
    check(c.ok && c.plan.ask.options.isEmpty(), "no card on a chat-only turn");
  }
  {
    const auto r = parseOpPlan(
        R"({"version":1,"reply":"pick","ask":{"question":"Which?","options":[{"label":"Sepia","actions":[{"op":"filter","mode":"sepia"}]},{"label":"Left","actions":[{"op":"rotate","dir":"left"}]}]}})");
    check(r.ok && r.plan.ask.options[0].actions.size() == 1, "preview actions parsed");
    check(r.plan.ask.options[1].actions.size() == 1, "each option keeps its own preview");
  }
  {
    const auto r = parseOpPlan(
        R"({"version":1,"reply":"pick","ask":{"question":"Which?","options":[{"label":"Web","image":{"url":"https://example.com/cat.jpg"}},{"label":"Saved","image":{"projectId":"p_12"}}]}})");
    check(r.ok && r.plan.ask.options[0].imageUrl == "https://example.com/cat.jpg", "http image reference kept");
    check(r.plan.ask.options[1].projectId == "p_12", "project reference kept");
  }
  {
    const auto r = parseOpPlan(
        R"({"version":1,"reply":"pick","ask":{"question":"Which?","options":[{"label":"Page","image":{"scanIndex":3}},{"label":"Plain"}]}})");
    check(r.ok && r.plan.ask.options.size() == 2, "a scan reference keeps the option");
    check(r.plan.ask.options[0].imageUrl.isEmpty() && !r.plan.warnings.isEmpty(), "...but loses the picture, with a warning");
  }
  {
    const char* bad[] = {
        R"({"version":1,"reply":"x","ask":"hello"})",
        R"({"version":1,"reply":"x","ask":{"options":[{"label":"A"},{"label":"B"}]}})",
        R"({"version":1,"reply":"x","ask":{"question":"  ","options":[{"label":"A"},{"label":"B"}]}})",
        R"({"version":1,"reply":"x","ask":{"question":"Q","options":[]}})",
        R"({"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"only"}]}})",
        R"({"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"1"},{"label":"2"},{"label":"3"},{"label":"4"},{"label":"5"},{"label":"6"}]}})",
        R"({"version":1,"reply":"x","ask":{"question":"Q","mode":"maybe","options":[{"label":"A"},{"label":"B"}]}})",
        R"({"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A"},{"label":"B"}],"sneaky":1}})",
        R"({"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A","sneaky":1},{"label":"B"}]}})",
        R"({"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":""},{"label":"B"}]}})",
        R"({"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A","actions":[],"image":{"url":"https://e/x"}},{"label":"B"}]}})",
        R"({"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A","image":{}},{"label":"B"}]}})",
        R"({"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A","image":{"url":"https://e/x","projectId":"p"}},{"label":"B"}]}})",
        R"({"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A","image":{"url":"data:image/png;base64,AA"}},{"label":"B"}]}})",
        R"({"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A","image":{"url":"file:///etc/passwd"}},{"label":"B"}]}})",
        R"({"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A"},{"label":"B"}],"allowCustom":"yes"}})",
    };
    bool allRejected = true;
    for (const char* json : bad) {
      if (parseOpPlan(QString::fromUtf8(json)).ok) allRejected = false;
    }
    check(allRejected, "every malformed card rejects the whole plan");
  }
  {
    // §1/§11.2: the misplaced op costs the PREVIEW; the option is still offered.
    const auto r = parseOpPlan(
        R"({"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"Dark","actions":[{"op":"theme","mode":"dark"}]},{"label":"B"}]}})");
    check(r.ok && r.plan.ask.options.size() == 2 &&
              r.plan.ask.options[0].label == "Dark" &&
              r.plan.ask.options[0].actions.isEmpty(),
          "an editor-settings op inside a preview drops the preview, not the plan");
    check(r.plan.warnings.size() == 1 &&
              r.plan.warnings[0].contains("preview for option 1 \"Dark\"") &&
              r.plan.warnings[0].contains("still offered"),
          "…with a warning naming the option");
  }
  {
    check(askAnswerText({"Sepia"}) == "Sepia", "one pick becomes the answer");
    check(askAnswerText({"Sepia", "B&W"}) == "Sepia, B&W", "several picks join");
    check(askAnswerText({}).isEmpty(), "no pick, no answer");
    check(askAnswerText({"Sepia"}, "  a warm green  ") == "a warm green", "typed text wins, trimmed");
    check(askAnswerText({"A", "   ", ""}) == "A", "blank labels never pad the answer");
    const int answerCap = OpSchema::desktop().limit("ask.answer");
    check(answerCap == 500 && askAnswerText({}, QString(answerCap + 20, 'x')).size() == answerCap,
          "answer capped at the registry's ask.answer limit");
  }

  // planTouchesTheImage: an EDITING plan arriving with an empty canvas takes the attached picture as the
  // working image; a question about it, or a settings change, leaves the canvas alone.
  {
    using stencil::llm::planTouchesTheImage;
    const auto planOf = [](const char* json) {
      return stencil::llm::parseOpPlan(QString::fromUtf8(json)).plan;
    };
    check(planTouchesTheImage(planOf(R"({"version":1,"reply":"","actions":[{"op":"filter","mode":"bw"}],"variants":[]})")),
          "a filter edits the image");
    check(planTouchesTheImage(planOf(R"({"version":1,"reply":"","actions":[{"op":"crop","spec":{"x1":"10%"}}],"variants":[]})")),
          "so does a crop");
    check(planTouchesTheImage(planOf(R"({"version":1,"reply":"","actions":[],"variants":[{"label":"a","actions":[{"op":"filter","mode":"bw"}]}]})")),
          "variants are alternatives OF the image, so they count");
    check(!planTouchesTheImage(planOf(R"({"version":1,"reply":"just answering","actions":[],"variants":[]})")),
          "a chat-only turn does not");
    check(!planTouchesTheImage(planOf(R"({"version":1,"reply":"","actions":[{"op":"theme","mode":"dark"}],"variants":[]})")),
          "and neither does a settings op");
    // §2.1: switching to an attachment (or saving) is not itself an edit — the
    // ops around it are what make the plan worth adopting a picture for.
    check(!planTouchesTheImage(planOf(R"({"version":1,"reply":"","actions":[{"op":"image","index":1},{"op":"save"}],"variants":[]})")),
          "image/save alone do not count as editing the picture");
  }

  }

}  // namespace llmopplan
