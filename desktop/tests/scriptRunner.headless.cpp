// The .stc runner (app/scriptRun.cpp) over a CanvasWidget-backed PlanTarget seeded with the
// committed PNG fixture (16x12 solid #3366cc). Asserts the DESKTOP half of contracts/stc:
// which op reaches which PlanTarget call, what an @undo reverts, and that an error runs nothing.
#include "models.hpp"
#include "planExecutor.hpp"
#include "scriptRun.hpp"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QStringList>
#include <QTemporaryDir>
#include <cstdio>

#include "support/check.hpp"

using stencil::gui::ScriptRunResult;
using stencil::gui::runScript;
using stencil::gui::runScriptFile;

namespace {

  // Records the calls a script makes that the canvas target itself refuses (it has no
  // files, no project store and no video), so each op's destination is observable.
  class RecordingTarget : public stencil::llm::CanvasPlanTarget {
   public:
    using CanvasPlanTarget::CanvasPlanTarget;

    bool openFile(const QString& path, QString*) override { opened << path; return true; }
    bool openUrl(const QString& url, bool, QString*) override { opened << url; return true; }
    bool openSourceFrame(const QString& spec, int frame, QString*) override {
      framedFrom << spec;
      frames << frame;
      return true;
    }
    bool saveProject(const QString& name, const QString&, QString*) override {
      saved << name;
      return true;
    }
    void setLayoutLines(const stencil::core::Lines& next) override {
      lines = next;
      CanvasPlanTarget::setLayoutLines(next);
    }
    void commitLayoutLines(const stencil::core::Lines& next) override {
      lines = next;
      CanvasPlanTarget::commitLayoutLines(next);
    }
    int stepHistory(bool redo, int steps) override {
      (redo ? redos : undos) << steps;
      return CanvasPlanTarget::stepHistory(redo, steps);
    }

    QStringList opened, saved, framedFrom;
    QVector<int> frames, undos, redos;
    stencil::core::Lines lines;
  };

  QImage fixtureImage() {
    QImage img;
    check(img.load(QStringLiteral(STENCIL_FIXTURES_DIR "/sample.png")), "fixture PNG loaded");
    return img;
  }

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);   // offscreen via QT_QPA_PLATFORM
  const stencil::core::PageSize a4{21.0, 29.7};

  std::printf("ops reach the target:\n");
  {
    RecordingTarget target(fixtureImage(), a4);
    const ScriptRunResult r = runScript(QStringLiteral(
        "@source https://example.com/a.png:\n"
        "  @frame 3\n"
        "  @crop 2px 2px 14px 10px\n"
        "  @filter bw\n"
        "  @save keep\n"), target);
    check(r.isOk, "a well-formed script runs");
    check(r.ops == 5, "every op ran");
    check(target.opened == QStringList{QStringLiteral("https://example.com/a.png")},
          "@source opened the url");
    check(target.frames == QVector<int>{3} &&
              target.framedFrom == QStringList{QStringLiteral("https://example.com/a.png")},
          "@frame re-opened the BLOCK's own source at that frame");
    check(target.saved == QStringList{QStringLiteral("keep")}, "@save named the project");
    const QImage out = target.renderResult();
    check(out.width() == 12 && out.height() == 8, "@crop resolved to 12x8");
    const QColor px = out.pixelColor(6, 4);
    check(px.red() == px.green() && px.green() == px.blue(), "@filter bw left a gray pixel");
  }

  std::printf("shapes become layout lines:\n");
  {
    RecordingTarget target(fixtureImage(), a4);
    const ScriptRunResult r = runScript(QStringLiteral(
        "@use line red dashed 3 fill #00ff00 point 5\n"
        "@line (1,1) (10,1) (10,8)\n"
        "@rect (2,2) (6,6)\n"), target);
    check(r.isOk, "shape ops run");
    check(target.lines.size() == 2, "both shapes landed as lines");
    if (target.lines.size() == 2) {
      const stencil::core::Line& line = target.lines[0];
      check(line.points.size() == 3, "@line kept its three points");
      check(!line.locked, "@line stays open");
      check(line.style == "dashed", "the @use style baked in");
      check(line.thickness == 3.0, "the @use thickness baked in");
      const stencil::core::Line& rect = target.lines[1];
      check(rect.points.size() == 4, "a two-corner @rect expands to four corners");
      check(rect.locked, "@rect is closed, so it fills");
    }
  }

  std::printf("@undo reverts the script's own edits:\n");
  {
    RecordingTarget target(fixtureImage(), a4);
    const ScriptRunResult r = runScript(QStringLiteral(
        "@filter bw\n"
        "@filter sepia\n"
        "@save one\n"
        "@undo 1\n"
        "@save two\n"
        "@redo\n"
        "@save three\n"), target);
    check(r.isOk, "a script with @undo and @redo runs");
    check(target.saved == QStringList{QStringLiteral("one"), QStringLiteral("two"),
                                      QStringLiteral("three")},
          "every @save around the history ops ran");
    // §7: the lowerer resolves history when it lowers, so neither reaches the canvas stack.
    check(target.undos.isEmpty() && target.redos.isEmpty(),
          "an @undo is a checkpoint restore, not a canvas undo");
    const QColor px = target.renderResult().pixelColor(8, 6);
    check(px.red() > px.blue(), "the replayed @filter sepia is what stands at the end");
  }

  std::printf("a shape combines with the layout, and @undo takes only its own back:\n");
  {
    RecordingTarget target(fixtureImage(), a4);
    stencil::core::Line kept;
    kept.points = {{1.0, 1.0}, {2.0, 2.0}};
    target.setLayoutLines({kept});   // what the user had drawn before the script
    const ScriptRunResult r = runScript(QStringLiteral(
        "@line (3,3) (9,3)\n"
        "@line (4,4) (9,9)\n"
        "@undo 1\n"), target);
    check(r.isOk, "the shapes and the @undo run");
    check(target.lines.size() == 2, "the user's line survived and one scripted line stands");
    if (target.lines.size() == 2) {
      check(target.lines[0].points.size() == 2 && target.lines[0].points[0].x == 1.0,
            "the line that was there first is still first");
      check(target.lines[1].points[0].x == 4.0, "and the surviving @line is the second one");
    }
  }

  std::printf("@undo reverts a crop:\n");
  {
    RecordingTarget once(fixtureImage(), a4);
    check(runScript(QStringLiteral("@crop 10%\n"), once).isOk, "one crop runs");
    RecordingTarget twice(fixtureImage(), a4);
    check(runScript(QStringLiteral("@crop 10%\n@crop 10%\n@undo 2\n"), twice).isOk,
          "two crops and an @undo run");
    check(twice.workingSize() == once.workingSize(),
          "the reverted crop left exactly one crop standing");
  }

  std::printf("@layout is reported, not skipped:\n");
  {
    RecordingTarget target(fixtureImage(), a4);
    const ScriptRunResult r = runScript(QStringLiteral(
        "@rect (1,1) (4,4)\n"
        "@layout marks.json\n"
        "@filter bw\n"), target);
    check(!r.isOk, "the desktop cannot read a layout file mid-script");
    check(r.ops == 1, "the @rect before it still counts as run");
    check(r.line == 2 && r.error.contains(QStringLiteral("@layout")),
          "the failure names line 2 and the directive");
    check(target.lines.size() == 1, "only the @rect reached setLayoutLines");
    const QColor px = target.renderResult().pixelColor(8, 6);
    check(px.red() != px.green(), "the @filter after it never ran");
  }

  std::printf("a bad script runs nothing:\n");
  {
    RecordingTarget target(fixtureImage(), a4);
    const ScriptRunResult r = runScript(QStringLiteral("@filter bw\n@nope 1\n"), target);
    check(!r.isOk, "the script failed");
    check(r.ops == 0, "no op ran, not even the good one before the error");
    check(r.line == 2, "the error names line 2");
    check(!r.error.isEmpty(), "the error carries a message");
    check(target.renderResult().pixelColor(4, 4).red() != target.renderResult().pixelColor(4, 4).blue(),
          "the image was never filtered");
  }

  std::printf("a script that opens its own source needs no image:\n");
  {
    RecordingTarget target(QImage(), a4);
    check(!target.hasImage(), "the target starts empty");
    const ScriptRunResult r = runScript(QStringLiteral(
        "@source https://example.com/a.png:\n  @filter bw\n"), target);
    check(r.isOk, "a @source-first script runs on an empty window");

    const ScriptRunResult bare = runScript(QStringLiteral("@filter bw\n"), target);
    check(!bare.isOk && bare.error.contains(QStringLiteral("open an image")),
          "one that edits straight away still asks for an image");
  }

  std::printf("an empty editor is not an error:\n");
  {
    RecordingTarget target(fixtureImage(), a4);
    const ScriptRunResult r = runScript(QStringLiteral("# just a comment\n"), target);
    check(r.isOk && r.ops == 0, "a comment-only script runs cleanly with nothing to do");
  }

  std::printf("a file that is not there reports like a script error:\n");
  {
    RecordingTarget target(fixtureImage(), a4);
    QTemporaryDir dir;
    const ScriptRunResult r = runScriptFile(dir.filePath(QStringLiteral("missing.stc")), target);
    check(!r.isOk, "a missing file fails");
    check(r.error.contains(QStringLiteral("missing.stc")), "the message names the file");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
