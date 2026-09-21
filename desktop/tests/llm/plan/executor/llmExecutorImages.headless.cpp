// Several images in one plan (§2.1), and the page/blank dimension forms (§2).
#include "llmExecutorParts.hpp"

using namespace stencil::llm;

namespace llmexec {

  void checkImageOps(const QImage& img, const stencil::core::PageSize& a4) {
  // ── §2.1 multi-image ops: `image` switches attachments, `save` persists ──
  std::printf("multi-image ops (s2.1):\n");
  {
    // The turn's attachments + the project saves, as the live target would do
    // them (MainWindow loads the attachment / creates a local project).
    struct MultiTarget : CanvasPlanTarget {
      using CanvasPlanTarget::CanvasPlanTarget;
      int attachments = 2;   // what THIS turn attached
      QVector<int> loaded;
      QStringList saved;
      QStringList savedTo;  // the destination each save was given ("" = the editor's own store)
      stencil::core::Lines got;
      bool loadAttachment(int index, QString* err) override {
        if (index < 1 || index > attachments) {
          if (err)
            *err = QStringLiteral("this message attached %1 image(s)").arg(attachments);
          return false;
        }
        loaded << index;
        return true;
      }
      bool saveProject(const QString& name, const QString& dest, QString*) override {
        saved << name;
        savedTo << dest;   // §10: "" unless the user named a destination
        return true;
      }
      void setLayoutLines(const stencil::core::Lines& lines) override {
        got = lines;
        CanvasPlanTarget::setLayoutLines(lines);
      }
    };
    {
      const auto parsed = parseOpPlan(R"({
        "reply": "both", "actions": [
          {"op": "image", "index": 1}, {"op": "filter", "mode": "bw"},
          {"op": "save", "name": "one"},
          {"op": "image", "index": 2}, {"op": "save", "name": "two"}
        ]})");
      check(parsed.ok, "multi-image plan parses");
      MultiTarget target(img, a4);
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok && res.notes.isEmpty(), "multi-image plan executes cleanly");
      check(target.loaded == QVector<int>({1, 2}), "each image op loaded THAT attachment");
      check(target.saved == QStringList({"one", "two"}), "one save per image, named");
    }
    {
      // An index the turn cannot satisfy costs that action, not the plan.
      const auto parsed = parseOpPlan(R"({
        "reply": "third", "actions": [
          {"op": "image", "index": 3}, {"op": "page", "format": "a5"}
        ]})");
      MultiTarget target(img, a4);
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok, "an out-of-range image index does not fail the plan");
      check(res.notes.size() == 1 && res.notes.first().contains("attached image 3") &&
                res.notes.first().contains("2 image(s)"),
            "…it lands as a skipped-action note naming the index");
      check(target.loaded.isEmpty(), "nothing was loaded");
      check(target.pageFormat == "A5", "the actions after it still ran");
    }
    {
      // Saving with nothing loaded is a skipped action too.
      const auto parsed =
          parseOpPlan(R"({"reply":"s","actions":[{"op":"save","name":"x"}]})");
      MultiTarget target(QImage(), a4);
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok, "save with no image does not fail the plan");
      check(res.notes.size() == 1 && res.notes.first().contains("no working image"),
            "…it warns instead");
      check(target.saved.isEmpty(), "and nothing was saved");
    }
    {
      // A crop moves the origin; the attachment after it is a FRESH frame, so the layout that follows lands
      // on the points as written (§1 re-mapping resets).
      const auto parsed = parseOpPlan(R"({
        "reply": "fresh frame", "actions": [
          {"op": "crop", "spec": {"x1": "2px", "y1": "2px"}},
          {"op": "image", "index": 1},
          {"op": "layout", "lines": [{"points": [{"x": 5, "y": 7}]}]}
        ]})");
      check(parsed.ok, "crop + image + layout plan parses");
      MultiTarget target(img, a4);
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok, "it executes");
      check(target.got.size() == 1 && target.got[0].points.size() == 1 &&
                near(target.got[0].points[0].x, 5) && near(target.got[0].points[0].y, 7),
            "switching image resets the §1 coordinate re-mapping");
    }
    {
      // A target with neither capability: the switch is a note, the save an error.
      CanvasPlanTarget target(img, a4);
      const auto p1 =
          parseOpPlan(R"({"reply":"i","actions":[{"op":"image","index":1}]})");
      const ExecResult r1 = executePlan(p1.plan, target);
      check(r1.ok && r1.notes.size() == 1, "a target that cannot switch images notes it");
      const auto p2 = parseOpPlan(R"({"reply":"s","actions":[{"op":"save"}]})");
      const ExecResult r2 = executePlan(p2.plan, target);
      check(!r2.ok && r2.error.contains("save"), "…and rejects the save outright");
    }
  }

  // ── §2 page custom dims / blank dims / formula enabled+clear ──
  std::printf("page custom / blank dims / formula forms (s2):\n");
  {
    const auto parsed = parseOpPlan(
        R"({"reply":"p","actions":[{"op":"page","width":20,"height":30}]})");
    check(parsed.ok, "custom-dims page plan parses");
    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "custom-dims page executes");
    check(target.pageCustomW == 20.0 && target.pageCustomH == 30.0,
          "custom dims recorded (cm)");
    check(target.pageFormat.isEmpty(), "the format path was not taken");
    check(target.pageCm().width == 20.0 && target.pageCm().height == 30.0,
          "the page metrics adopted the custom size");
  }
  {
    const auto parsed = parseOpPlan(
        R"({"reply":"b","actions":[{"op":"blank","color":"#123456","width":10,"height":5}]})");
    check(parsed.ok, "blank-with-dims plan parses");
    CanvasPlanTarget target(QImage(), a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "blank with explicit cm dims executes");
    const stencil::core::SizePx px =
        stencil::core::defaultBlankSizePx({10.0, 5.0}, 96.0);
    const QImage result = target.renderResult();
    check(result.width() == px.width && result.height() == px.height,
          "explicit dims size the blank (core defaultBlankSizePx)");
  }
  {
    // An empty expr clears the axis; the `enabled` form flips the toggle.
    const auto parsed = parseOpPlan(R"({
      "reply": "f", "actions": [
        {"op": "formula", "axis": "x", "expr": "x*2"},
        {"op": "formula", "axis": "x", "expr": ""},
        {"op": "formula", "enabled": false}
      ]})");
    check(parsed.ok, "formula clear + enabled plan parses");
    CanvasPlanTarget target(img, a4);
    const ExecResult res = executePlan(parsed.plan, target);
    check(res.ok, "formula clear + enabled executes");
    check(target.formulaX.isEmpty(), "the empty expr cleared the x axis");
    check(target.formulasEnabled == 0, "enabled:false switched formulas OFF");
  }

  }

}  // namespace llmexec
