// Rotate, filter, layout and formula: the per-op field matrices.
#include "llmOpPlanParts.hpp"

using namespace stencil::llm;

namespace llmopplan {

  void checkShapeOps() {
  // ── rotate ──
  std::printf("rotate:\n");
  check(!parseOpPlan("{\"reply\":\"r\",\"actions\":[{\"op\":\"rotate\",\"dir\":\"up\"}]}").ok,
        "bad rotate dir fails");
  check(!parseOpPlan("{\"reply\":\"r\",\"actions\":[{\"op\":\"rotate\",\"dir\":\"left\","
                     "\"times\":0}]}")
             .ok,
        "times 0 out of range");
  check(!parseOpPlan("{\"reply\":\"r\",\"actions\":[{\"op\":\"rotate\",\"dir\":\"left\","
                     "\"times\":4}]}")
             .ok,
        "times 4 out of range");
  check(!parseOpPlan("{\"reply\":\"r\",\"actions\":[{\"op\":\"rotate\",\"dir\":\"left\","
                     "\"times\":1.5}]}")
             .ok,
        "fractional times fails");

  // ── filter ──
  std::printf("filter:\n");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"f\",\"actions\":[{\"op\":\"filter\",\"mode\":\"custom\","
        "\"tint\":\"#A1b2C3\"}]}");
    check(r.ok && r.plan.actions[0].tint == "#A1b2C3", "custom filter with hex tint");
  }
  check(parseOpPlan("{\"reply\":\"f\",\"actions\":[{\"op\":\"filter\",\"mode\":\"contour\"}]}").ok,
        "contour filter parses");
  check(!parseOpPlan("{\"reply\":\"f\",\"actions\":[{\"op\":\"filter\",\"mode\":\"custom\"}]}").ok,
        "custom without tint fails");
  check(!parseOpPlan("{\"reply\":\"f\",\"actions\":[{\"op\":\"filter\",\"mode\":\"bw\","
                     "\"tint\":\"#112233\"}]}")
             .ok,
        "tint forbidden unless custom");
  check(!parseOpPlan("{\"reply\":\"f\",\"actions\":[{\"op\":\"filter\",\"mode\":\"custom\","
                     "\"tint\":\"#12z\"}]}")
             .ok,
        "malformed tint hex fails");
  check(!parseOpPlan("{\"reply\":\"f\",\"actions\":[{\"op\":\"filter\",\"mode\":\"blur\"}]}").ok,
        "unknown filter mode fails");

  // ── layout ──
  std::printf("layout:\n");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"l\",\"actions\":[{\"op\":\"layout\",\"lines\":[{\"points\":"
        "[{\"x\":1,\"y\":2},{\"x\":3,\"y\":4}]}]}]}");
    check(r.ok && r.plan.actions[0].lines.size() == 1, "minimal line parses");
    const auto& line = r.plan.actions[0].lines[0];
    check(line.color == "#FFFF00" && line.thickness == 2.0 && line.pointSize == 4.0 &&
              line.style == "solid" && !line.locked && line.fillColor == "transparent",
          "per-line defaults applied (contract §3)");
  }
  check(!parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"layout\",\"lines\":[{"
                     "\"points\":[{\"x\":1,\"y\":2}],\"style\":\"wavy\"}]}]}")
             .ok,
        "unknown line style fails");
  check(!parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"layout\",\"lines\":[{"
                     "\"points\":[{\"x\":1,\"y\":2}],\"width\":3}]}]}")
             .ok,
        "unknown line field fails");
  check(!parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"layout\",\"lines\":[{"
                     "\"points\":[]}]}]}")
             .ok,
        "empty points fails");
  {
    // Valid but draws nothing — the §7 shape test must not count it as "drew".
    const auto r =
        parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"layout\",\"lines\":[]}]}");
    check(r.ok && r.plan.actions[0].lines.empty(), "empty lines array parses (zero lines)");
  }
  {
    QString lines;
    for (int i = 0; i < 201; ++i)
      lines += QString("%1{\"points\":[{\"x\":1,\"y\":2}]}").arg(i ? "," : "");
    check(!parseOpPlan("{\"reply\":\"l\",\"actions\":[{\"op\":\"layout\",\"lines\":[" +
                       lines + "]}]}")
               .ok,
          "201 lines exceed the 200 limit");
  }

  // ── formula ──
  std::printf("formula:\n");
  {
    const auto r = parseOpPlan(
        "{\"reply\":\"f\",\"actions\":[{\"op\":\"formula\",\"axis\":\"x\","
        "\"expr\":\"x**2 + 10\"}]}");
    check(r.ok && r.plan.actions[0].axis == QChar('x'), "x-axis formula parses (** allowed)");
  }
  check(!parseOpPlan("{\"reply\":\"f\",\"actions\":[{\"op\":\"formula\",\"axis\":\"x\","
                     "\"expr\":\"y*2\"}]}")
             .ok,
        "variable must match the axis");
  check(!parseOpPlan("{\"reply\":\"f\",\"actions\":[{\"op\":\"formula\",\"axis\":\"y\","
                     "\"expr\":\"y^2\"}]}")
             .ok,
        "charset rejects ^");
  check(!parseOpPlan("{\"reply\":\"f\",\"actions\":[{\"op\":\"formula\",\"axis\":\"z\","
                     "\"expr\":\"1\"}]}")
             .ok,
        "axis must be x or y");
  {
    const QString big(5001, QChar('1'));
    check(!parseOpPlan("{\"reply\":\"f\",\"actions\":[{\"op\":\"formula\",\"axis\":\"x\","
                       "\"expr\":\"" + big + "\"}]}")
               .ok,
          "5001-char expr exceeds the 5000 limit");
  }

  {
    // §2: an empty expr CLEARS that axis (identity) — no longer a parse error.
    const auto r = parseOpPlan(
        "{\"reply\":\"f\",\"actions\":[{\"op\":\"formula\",\"axis\":\"y\",\"expr\":\"\"}]}");
    check(r.ok && r.plan.actions[0].axis == QChar('y') && r.plan.actions[0].expr.isEmpty(),
          "empty expr parses as a clear of that axis");
  }
  {
    // §2: `enabled` is a bool ALONE — false switches formulas OFF.
    const auto r = parseOpPlan(
        "{\"reply\":\"f\",\"actions\":[{\"op\":\"formula\",\"enabled\":false}]}");
    check(r.ok && r.plan.actions[0].formulaEnabled == 0, "enabled:false parses alone");
    const auto on = parseOpPlan(
        "{\"reply\":\"f\",\"actions\":[{\"op\":\"formula\",\"enabled\":true}]}");
    check(on.ok && on.plan.actions[0].formulaEnabled == 1, "enabled:true parses alone");
    check(!parseOpPlan("{\"reply\":\"f\",\"actions\":[{\"op\":\"formula\","
                       "\"enabled\":false,\"axis\":\"x\",\"expr\":\"x\"}]}")
               .ok,
          "enabled combined with axis/expr fails");
    check(!parseOpPlan("{\"reply\":\"f\",\"actions\":[{\"op\":\"formula\","
                       "\"enabled\":1}]}")
               .ok,
          "non-boolean enabled fails");
  }

  }

}  // namespace llmopplan
