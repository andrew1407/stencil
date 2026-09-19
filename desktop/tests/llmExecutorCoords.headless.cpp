// Model-frame layout coordinates re-mapped through earlier crop/rotate, then clamped (§1).
#include "llmExecutorParts.hpp"

using namespace stencil::llm;

namespace llmexec {

  void checkCoordinateRemapping(const QImage& img, const stencil::core::PageSize& a4) {
  // ── §1 coordinate re-mapping: plan coords are model-frame ──
  std::printf("coordinate re-mapping (s1):\n");
  // Captures the lines the executor hands to the canvas (post-map, post-clamp).
  {
    // crop-then-layout: the crop origin (2,2) is subtracted from later points;
    // a point that lands left of the crop clamps to the new frame's edge.
    const auto parsed = parseOpPlan(R"({
      "reply": "crop then draw",
      "actions": [
        {"op": "crop", "spec": {"x1": "2px", "y1": "2", "x2": "14px", "y2": "10px"}},
        {"op": "layout", "lines": [
          {"points": [{"x": 2, "y": 6}, {"x": 13.5, "y": 6.25}, {"x": 0, "y": 0}],
           "color": "#FF0000", "thickness": 3}
        ]}
      ]
    })");
    check(parsed.ok, "crop+layout plan parses");
    LayoutRecorder target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "crop+layout plan executes");
    check(target.got.size() == 1 && target.got[0].points.size() == 3,
          "layout line reached the canvas");
    if (target.got.size() == 1 && target.got[0].points.size() == 3) {
      const auto& p = target.got[0].points;
      check(near(p[0].x, 0) && near(p[0].y, 4), "point translated by -crop origin");
      check(near(p[1].x, 11.5) && near(p[1].y, 4.25), "fractional point translated too");
      check(near(p[2].x, 0) && near(p[2].y, 0), "point left of the crop clamps to 0,0");
    }
    const QImage result = target.renderResult();
    check(result.width() == 12 && result.height() == 8, "cropped frame is 12x8");
    const QColor c = result.pixelColor(6, 4);
    check(c.red() > 200 && c.green() < 100, "re-mapped line draws inside the crop");
  }
  {
    // Crop with the §2 aspect key: the executor forwards it into the core
    // CropSpec, and resolveCropRect shrinks the wider dimension symmetrically
    // about the centre — 16x12 at "1:1" lands on the centred 12x12 square.
    const auto parsed = parseOpPlan(R"({
      "reply": "square",
      "actions": [{"op": "crop", "spec": {"aspect": "1:1"}}]
    })");
    check(parsed.ok, "aspect-only crop plan parses");
    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok && res.changed, "aspect-only crop executes");
    const QImage result = target.renderResult();
    check(result.width() == 12 && result.height() == 12,
          "aspect crop reached core resolveCropRect (16x12 -> centred 12x12)");
  }
  {
    // rotate-then-layout, direction validated against core's rotate: mark the
    // pixel (3,2), rotate right; core rotateImageRGBA (quarters=1, clockwise)
    // moves the mark to (h-1-y, x) = (9,3) — the app's rotate and the
    // executor's point mapping must land the same place.
    std::vector<std::uint8_t> src(16 * 12 * 4, 0), dst(16 * 12 * 4, 0);
    src[(2 * 16 + 3) * 4] = 255;  // mark (3,2)
    stencil::core::rotateImageRGBA(src.data(), 16, 12, 1, dst.data());
    check(dst[(3 * 12 + 9) * 4] == 255, "core rotate cw puts (3,2) at (9,3)");

    // The app's rotate agrees: rotate-only plan on a marked image.
    QImage marked(16, 12, QImage::Format_RGB32);
    marked.fill(QColor(0x33, 0x66, 0xcc));
    marked.setPixelColor(3, 2, QColor(Qt::red));
    {
      const auto rotOnly =
          parseOpPlan(R"({"reply":"r","actions":[{"op":"rotate","dir":"right"}]})");
      CanvasPlanTarget target(marked, a4);
      check(executePlan(rotOnly.plan, target).ok, "rotate-only plan executes");
      const QImage result = target.renderResult();
      check(result.width() == 12 && result.height() == 16, "rotated frame is 12x16");
      const QColor mc = result.pixelColor(9, 3);
      check(mc.red() > 200 && mc.blue() < 100, "app rotate matches core: mark at (9,3)");
    }
    // And the executor's point mapping follows the same turn.
    const auto parsed = parseOpPlan(R"({
      "reply": "rotate then draw",
      "actions": [
        {"op": "rotate", "dir": "right"},
        {"op": "layout", "lines": [
          {"points": [{"x": 3.5, "y": 2.5}, {"x": 14, "y": 6}],
           "color": "#00FF00", "thickness": 1}
        ]}
      ]
    })");
    check(parsed.ok, "rotate+layout plan parses");
    LayoutRecorder target(marked, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "rotate+layout plan executes");
    check(target.got.size() == 1 && target.got[0].points.size() == 2,
          "rotated layout line reached the canvas");
    if (target.got.size() == 1 && target.got[0].points.size() == 2) {
      const auto& p = target.got[0].points;
      // cw mapping (x,y) -> (h-y, x) with h=12: the marked pixel's center
      // (3.5,2.5) lands in the same pixel core moved the mark to; (14,6)->(6,14).
      check(near(p[0].x, 9.5) && near(p[0].y, 3.5),
            "point followed the marked pixel through the rotate");
      check(near(p[1].x, 6) && near(p[1].y, 14), "second point quarter-turned");
    }
  }
  {
    // clamp on a plan with NO crop/rotate: out-of-bounds points pull into the
    // working image's bounds before drawing.
    const auto parsed = parseOpPlan(R"({
      "reply": "clamped",
      "actions": [{"op": "layout", "lines": [
        {"points": [{"x": -5, "y": 6}, {"x": 100, "y": 20}],
         "color": "#FF0000", "thickness": 3}
      ]}]
    })");
    check(parsed.ok, "clamp plan parses");
    LayoutRecorder target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "clamp plan executes");
    check(target.got.size() == 1 && target.got[0].points.size() == 2,
          "clamped line reached the canvas");
    if (target.got.size() == 1 && target.got[0].points.size() == 2) {
      const auto& p = target.got[0].points;
      check(near(p[0].x, 0) && near(p[0].y, 6), "negative x clamps to 0");
      check(near(p[1].x, 16) && near(p[1].y, 12), "overshoot clamps to the 16x12 bounds");
    }
  }
  {
    // A variant branches from the post-actions state — its model-frame coords
    // inherit the top-level crop's translation.
    const auto parsed = parseOpPlan(R"({
      "reply": "variant inherits the map",
      "actions": [
        {"op": "crop", "spec": {"x1": "2px", "y1": "2", "x2": "14px", "y2": "10px"}}
      ],
      "variants": [
        {"label": "drawn", "actions": [{"op": "layout", "lines": [
          {"points": [{"x": 2, "y": 6}, {"x": 13, "y": 6}],
           "color": "#FF0000", "thickness": 3}
        ]}]}
      ]
    })");
    check(parsed.ok, "variant re-map plan parses");
    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "variant re-map plan executes");
    check(res.variants.size() == 1, "one variant image produced");
    if (res.variants.size() == 1) {
      const QImage& v = res.variants[0].second;
      check(v.width() == 12 && v.height() == 8, "variant keeps the cropped frame");
      // (9,4) is on the stroke away from the point/midpoint markers.
      const QColor on = v.pixelColor(9, 4);
      check(on.red() > 200 && on.green() < 100, "variant line translated to y=4");
      const QColor off = v.pixelColor(6, 7);
      check(off.red() < 150, "no line at the un-translated y");
    }
  }

  }

}  // namespace llmexec
