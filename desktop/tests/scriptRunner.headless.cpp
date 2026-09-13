// Headless check of the .stc runner (app/scriptRun.cpp) over a CanvasWidget-backed
// PlanTarget seeded with the committed PNG fixture (16x12 solid #3366cc). The language
// is normative in contracts/stc/stc-contract.md; this asserts the DESKTOP half of it —
// which op reaches which PlanTarget call, and that an erroring script runs nothing.
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

    bool openFile(const QString& path, QString*) override {
      opened << path;
      return true;
    }
    bool openUrl(const QString& url, bool, QString*) override {
      opened << url;
      return true;
    }
    bool extractFrames(const QVector<int>& indices, QString*) override {
      for (int i : indices) frames << i;
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

    QStringList opened, saved;
    QVector<int> frames;
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
    check(r.ok, "a well-formed script runs");
    check(r.ops == 5, "every op ran");
    check(target.opened == QStringList{QStringLiteral("https://example.com/a.png")},
          "@source opened the url");
    check(target.frames == QVector<int>{3}, "@frame reached extractFrames");
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
    check(r.ok, "shape ops run");
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

  std::printf("a bad script runs nothing:\n");
  {
    RecordingTarget target(fixtureImage(), a4);
    const ScriptRunResult r = runScript(QStringLiteral("@filter bw\n@nope 1\n"), target);
    check(!r.ok, "the script failed");
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
    check(r.ok, "a @source-first script runs on an empty window");

    const ScriptRunResult bare = runScript(QStringLiteral("@filter bw\n"), target);
    check(!bare.ok && bare.error.contains(QStringLiteral("open an image")),
          "one that edits straight away still asks for an image");
  }

  std::printf("an empty editor is not an error:\n");
  {
    RecordingTarget target(fixtureImage(), a4);
    const ScriptRunResult r = runScript(QStringLiteral("# just a comment\n"), target);
    check(r.ok && r.ops == 0, "a comment-only script runs cleanly with nothing to do");
  }

  std::printf("a file that is not there reports like a script error:\n");
  {
    RecordingTarget target(fixtureImage(), a4);
    QTemporaryDir dir;
    const ScriptRunResult r = runScriptFile(dir.filePath(QStringLiteral("missing.stc")), target);
    check(!r.ok, "a missing file fails");
    check(r.error.contains(QStringLiteral("missing.stc")), "the message names the file");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
