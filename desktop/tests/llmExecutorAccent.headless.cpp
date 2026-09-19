// The accent presets, what copy copies, and the blank fill colour (§10).
#include "llmExecutorParts.hpp"

using namespace stencil::llm;

namespace llmexec {

  void checkAccentOps(const QImage& img, const stencil::core::PageSize& a4) {
  // ── §10 accent preset / copy what ──
  std::printf("accent preset / copy what (s10):\n");
  {
    const auto parsed = parseOpPlan(
        R"({"reply":"a","actions":[{"op":"accent","preset":"green"}]})");
    check(parsed.ok, "accent preset parses");
    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok && target.accentPreset == "green" && target.accentColor.isEmpty(),
          "the preset form takes the preset path, not the hex one");
  }
  {
    // An unknown preset name is the target's note+skip (contract §10).
    struct PresetTarget : CanvasPlanTarget {
      using CanvasPlanTarget::CanvasPlanTarget;
      void setAccentPreset(const QString& preset, QString* note) override {
        if (preset != "green") *note = QStringLiteral("unknown accent preset \"%1\"").arg(preset);
        else accentPreset = preset;
      }
    };
    PresetTarget target(img, a4);
    const ExecResult res = executePlan(
        parseOpPlan(R"({"reply":"a","actions":[{"op":"accent","preset":"sparkle"}]})").plan,
        target);
    check(res.ok && res.notes.size() == 1 && res.notes.first().contains("unknown accent preset"),
          "an unknown preset is a note + skip, never a failed plan");
  }
  {
    // copy what:"layout" needs drawn lines: a note without them, the layout
    // clipboard path with them.
    const auto parsed = parseOpPlan(
        R"({"reply":"c","actions":[{"op":"copy","what":"layout"}]})");
    check(parsed.ok, "copy what:layout parses");
    {
      CanvasPlanTarget target(img, a4);
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok && !target.copiedLayout, "no drawn lines: nothing copied");
      check(res.notes.size() == 1 && res.notes.first().contains("no drawn lines"),
            "…and the skip is a note");
    }
    {
      CanvasPlanTarget target(img, a4);
      const auto both = parseOpPlan(R"({
        "reply": "c", "actions": [
          {"op": "layout", "lines": [{"points": [{"x": 1, "y": 1}, {"x": 5, "y": 5}]}]},
          {"op": "copy", "what": "layout"}
        ]})");
      const ExecResult res = executePlan(both.plan, target);
      check(res.ok && target.copiedLayout && !target.copied,
            "with lines drawn, copy what:layout takes the layout path only");
    }
  }

  // ── §10 blankColor: blanks only (note+skip), keeps the drawn lines ──
  std::printf("blankColor (s10):\n");
  {
    // On a NON-blank image the recolour is a note + skip, and pixels survive.
    const auto parsed = parseOpPlan(
        R"({"reply":"b","actions":[{"op":"blankColor","color":"#ff0000"}]})");
    check(parsed.ok, "blankColor plan parses");
    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "blankColor on a photo does not fail the plan");
    check(res.notes.size() == 1 &&
              res.notes.first().contains("only a blank"),
          "…it lands as the blanks-only note");
    const QColor kept = target.renderResult().pixelColor(8, 6);
    check(kept.red() != 255 || kept.green() != 0, "the photo's pixels were left alone");
  }
  {
    // On a blank it recolours in place and KEEPS the drawn lines.
    const auto parsed = parseOpPlan(R"({
      "reply": "b", "actions": [
        {"op": "blank", "color": "red", "format": "a10"},
        {"op": "layout", "lines": [{"points": [{"x": 10, "y": 70}, {"x": 90, "y": 70}],
                                    "color": "#00FF00", "thickness": 3}]},
        {"op": "blankColor", "color": "#0000ff"}
      ]})");
    check(parsed.ok, "blank + layout + blankColor plan parses");
    CanvasPlanTarget target(QImage(), a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok && res.notes.isEmpty(), "recolouring a blank is not a note");
    const QImage result = target.renderResult();
    const QColor bg = result.pixelColor(50, 20);
    check(bg.blue() == 255 && bg.red() == 0, "the background recoloured to blue");
    const QColor line = result.pixelColor(50, 70);
    check(line.green() > 200 && line.blue() < 100, "…and the drawn line survived");
  }

  }

}  // namespace llmexec
