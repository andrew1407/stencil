// Plan shape: actions with variants, a dropped variant, and layout.
#include "llmExecutorParts.hpp"

using namespace stencil::llm;

namespace llmexec {

  void checkPlanBasics(const QImage& img, const stencil::core::PageSize& a4) {
  // ── actions + 2 variants ──
  std::printf("actions + variants:\n");
  {
    const auto parsed = parseOpPlan(R"({
      "version": 1,
      "reply": "cropped, rotated, desaturated; two variants",
      "actions": [
        {"op": "crop", "spec": {"x1": "2px", "y1": "2", "x2": "14px", "y2": "10px"}},
        {"op": "rotate", "dir": "right"},
        {"op": "filter", "mode": "bw"},
        {"op": "formula", "axis": "x", "expr": "x*2"},
        {"op": "page", "format": "a5"}
      ],
      "variants": [
        {"label": "Sepia one", "actions": [{"op": "filter", "mode": "sepia"}]},
        {"label": "tinted!", "actions": [
          {"op": "filter", "mode": "custom", "tint": "#ff0000"},
          {"op": "rotate", "dir": "left"}
        ]}
      ]
    })");
    check(parsed.ok, "plan parses");

    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "plan executes");
    check(res.changed, "top-level actions reported as changes");

    // crop 2..14 x 2..10 -> 12x8, then one clockwise quarter -> 8x12.
    const QImage result = target.renderResult();
    check(result.width() == 8 && result.height() == 12,
          "crop (12x8) + rotate right -> 8x12 result");
    {
      const QColor c = result.pixelColor(4, 6);
      check(c.red() == c.green() && c.green() == c.blue(),
            "bw filter left a grayscale pixel");
      check(c.red() > 0 && c.red() < 255, "gray value is a real mix (not black/white)");
    }
    check(target.formulaX == "x*2" && target.formulaY.isEmpty(),
          "formula op recorded on its axis");
    check(target.pageFormat == "A5", "page op adopted (uppercased canonical name)");

    // Variants: branch from the post-actions state (8x12 bw), one image each.
    check(res.variants.size() == 2, "two variant images produced");
    if (res.variants.size() == 2) {
      check(res.variants[0].first == "Sepia one", "variant label kept");
      check(res.variants[1].first == "tinted", "variant label sanitized");
      const QImage& sepia = res.variants[0].second;
      check(sepia.width() == 8 && sepia.height() == 12,
            "sepia variant keeps the post-actions size");
      const QColor sc = sepia.pixelColor(4, 6);
      check(sc.red() > sc.blue(), "sepia variant pixel is warm (r > b)");
      const QImage& tinted = res.variants[1].second;
      check(tinted.width() == 12 && tinted.height() == 8,
            "tinted variant applied its own rotate (12x8)");
      const QColor tc = tinted.pixelColor(6, 4);
      check(tc.red() > tc.blue() && tc.red() > tc.green(),
            "custom #ff0000 tint reddens the variant");
    }

    // The variants never touched the working target.
    check(target.renderResult().width() == 8, "working image untouched by variants");
  }

  // ── §1 leniency: a variant carrying a settings op is dropped, not fatal ──
  std::printf("dropped variant (contract 1):\n");
  {
    const auto parsed = parseOpPlan(R"({
      "reply": "rotated, plus one variant",
      "actions": [{"op": "rotate", "dir": "right"}],
      "variants": [
        {"label": "wiped", "actions": [{"op": "clear"}]},
        {"label": "sepia", "actions": [{"op": "filter", "mode": "sepia"}]}
      ]
    })");
    check(parsed.ok && parsed.error.isEmpty(), "the plan parses instead of failing");
    check(parsed.plan.warnings.size() == 1 &&
              parsed.plan.warnings[0].contains("Dropped variant \"wiped\""),
          "the dropped variant is a warning, not an error");

    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok && res.changed, "the top-level actions still execute");
    check(target.renderResult().width() == 12 && target.renderResult().height() == 16,
          "the rotate ran (16x12 -> 12x16)");
    check(!target.cleared, "the misplaced clear never reached the editor");
    check(res.variants.size() == 1 && res.variants[0].first == "sepia",
          "only the well-formed variant renders");
  }

  // ── layout draws onto the render ──
  std::printf("layout:\n");
  {
    const auto parsed = parseOpPlan(R"({
      "reply": "line drawn",
      "actions": [{"op": "layout", "lines": [
        {"points": [{"x": 0, "y": 6}, {"x": 15, "y": 6}],
         "color": "#FF0000", "thickness": 3}
      ]}]
    })");
    check(parsed.ok, "layout plan parses");
    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "layout plan executes");
    const QImage result = target.renderResult();
    check(result.size() == img.size(), "layout leaves the pixels' size alone");
    const QColor c = result.pixelColor(8, 6);
    check(c.red() > 200 && c.green() < 100, "the drawn line shows in the render");
  }

  }

}  // namespace llmexec
