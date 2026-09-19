// Undo/redo, compare/zoom and line-style widening (§2, §10).
#include "llmExecutorParts.hpp"

using namespace stencil::llm;

namespace llmexec {

  void checkHistoryOps(const QImage& img, const stencil::core::PageSize& a4) {
  // ── §2 undo / redo ──
  std::printf("undo/redo (s2):\n");
  {
    // A recorder with a bounded history: the executor asks for the steps, the
    // target reports how many actually ran, and the shortfall becomes a note.
    struct HistoryTarget : CanvasPlanTarget {
      using CanvasPlanTarget::CanvasPlanTarget;
      int undoAvailable = 1, redoAvailable = 0;
      QStringList calls;
      int stepHistory(bool redo, int steps) override {
        int& avail = redo ? redoAvailable : undoAvailable;
        const int done = std::min(avail, steps);
        avail -= done;
        calls << QStringLiteral("%1:%2").arg(redo ? "redo" : "undo").arg(done);
        return done;
      }
    };
    {
      const auto parsed =
          parseOpPlan(R"({"reply":"u","actions":[{"op":"undo","steps":3}]})");
      check(parsed.ok, "undo steps=3 parses");
      HistoryTarget target(img, a4);
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok && target.calls == QStringList{"undo:1"},
            "undo reached the target's history");
      check(res.notes.size() == 1 &&
                res.notes.first() == "undo: only 1 of 3 step(s) available",
            "running out of steps is a note, never a failed plan");
    }
    {
      const auto parsed = parseOpPlan(R"({"reply":"r","actions":[{"op":"redo"}]})");
      HistoryTarget target(img, a4);
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok && target.calls == QStringList{"redo:0"}, "redo defaults to one step");
      check(res.notes.size() == 1 && res.notes.first() == "redo: nothing to redo",
            "an empty history is the nothing-to-redo note");
    }
    {
      // The default CanvasPlanTarget drives the real canvas history stack —
      // empty here, so one undo lands as the nothing-to-undo note.
      const auto parsed = parseOpPlan(R"({"reply":"u","actions":[{"op":"undo"}]})");
      CanvasPlanTarget target(img, a4);
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok && res.notes.size() == 1 && res.notes.first().contains("nothing to undo"),
            "the canvas target's empty history notes, not fails");
    }
    {
      // A surface with NO history at all (the base default) fails the plan.
      struct NoHistoryTarget : CanvasPlanTarget {
        using CanvasPlanTarget::CanvasPlanTarget;
        int stepHistory(bool redo, int steps) override {
          return PlanTarget::stepHistory(redo, steps);
        }
      };
      NoHistoryTarget target(img, a4);
      const ExecResult res = executePlan(
          parseOpPlan(R"({"reply":"u","actions":[{"op":"undo"}]})").plan, target);
      check(!res.ok && res.error.contains("history is not available"),
            "a history-less surface rejects undo outright");
    }
  }

  // ── §10 compare / zoom (view-only) ──
  std::printf("compare/zoom (s10):\n");
  {
    const auto parsed = parseOpPlan(R"({
      "reply": "v", "actions": [
        {"op": "compare", "mode": "vertical", "split": 0.25},
        {"op": "zoom", "percent": 150}
      ]})");
    check(parsed.ok, "compare + zoom plan parses");
    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "compare + zoom execute");
    check(target.compareMode == "vertical" && target.compareSplit == 0.25,
          "compare mode + split recorded");
    check(target.zoomPercent == 150 && !target.zoomFit, "zoom percent recorded");
    check(target.renderResult().size() == img.size(),
          "view ops never touch the working image");
  }
  {
    const auto parsed = parseOpPlan(R"({"reply":"z","actions":[{"op":"zoom","fit":true}]})");
    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok && target.zoomFit, "zoom fit:true reaches the fit path");
  }
  {
    // Vertical, then a follow-up "none": the split view clears. (An echoed
    // "split" beside "none" is a parse failure since the registry's onlyWith rule —
    // fixture 160 — so the follow-up carries the mode alone.)
    CanvasPlanTarget target(img, a4);
    const ExecResult on = executePlan(
        parseOpPlan(R"({"reply":"v","actions":[
          {"op":"compare","mode":"vertical","split":0.3}]})").plan, target);
    const ExecResult off = executePlan(
        parseOpPlan(R"({"reply":"n","actions":[
          {"op":"compare","mode":"none"}]})").plan, target);
    check(on.ok && off.ok, "compare vertical then none both execute");
    check(target.compareMode == "none", "compare none clears the split view");
  }

  // ── §10 lineStyle widening: pointColor / drawMode apply, fillColor is a
  //    BROWSER control — the desktop notes+skips that field ──
  std::printf("lineStyle widening (s10):\n");
  {
    const auto parsed = parseOpPlan(R"({
      "reply": "ls", "actions": [{"op": "lineStyle",
        "color": "#00ff00", "pointColor": "", "drawMode": "rect",
        "fillColor": "#112233"}]})");
    check(parsed.ok, "widened lineStyle parses");
    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "widened lineStyle executes");
    check(target.lsColor == "#00ff00", "stroke colour still applied");
    check(target.lsPointColorSet && target.lsPointColor.isEmpty(),
          "pointColor \"\" recorded as the explicit follow-the-stroke value");
    check(target.lsDrawMode == "rect", "drawMode applied");
    check(res.notes.size() == 1 &&
              res.notes.first().contains("fillColor is a browser-editor control"),
          "fillColor lands as the desktop's note+skip");
  }
  {
    // fillColor alone: nothing to apply here, but the note still says why.
    const auto parsed = parseOpPlan(
        R"({"reply":"ls","actions":[{"op":"lineStyle","fillColor":"transparent"}]})");
    check(parsed.ok, "fillColor-only lineStyle parses");
    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok && res.notes.size() == 1 &&
              res.notes.first().contains("browser-editor control"),
          "fillColor-only lineStyle is a pure note");
    check(target.lsColor.isEmpty() && !target.lsPointColorSet,
          "…and applied nothing else");
  }

  }

}  // namespace llmexec
