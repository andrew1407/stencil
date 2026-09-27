// The .stc runner (app/scriptRun.cpp) across the loads that wait on I/O: runScriptThen returns with
// the run parked at an @source or @frame, each answer resumes it at the next op, and a failed or
// abandoned load ends it as a synchronous one did. Offscreen, over the committed 16x12 fixture.
#include "ScriptDoc.hpp"
#include "planExecutor.hpp"
#include "scriptRun.hpp"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QStringList>
#include <cstdio>
#include <memory>
#include <optional>

#include "../support/check.hpp"

using stencil::gui::ScriptRunResult;
using stencil::llm::OpDone;

namespace {

  constexpr char SCRIPT[] =
      "@source https://example.com/a.png:\n"
      "  @frame 3\n"
      "  @filter bw\n"
      "  @rect (2,2) (6,6)\n";

  // Holds each load's answer until the test gives it, logging the order the ops ran in.
  struct HeldTarget : stencil::llm::CanvasPlanTarget {
    using CanvasPlanTarget::CanvasPlanTarget;
    QStringList log;
    OpDone pending;
    stencil::core::Lines lines;
    void openUrlThen(const QString& url, bool, OpDone done) override {
      log << QStringLiteral("open %1").arg(url);
      pending = std::move(done);
    }
    void openSourceFrameThen(const QString&, int frame, OpDone done) override {
      log << QStringLiteral("frame %1").arg(frame);
      pending = std::move(done);
    }
    void setImageFilter(const QString& mode, const QString& tint) override {
      log << QStringLiteral("filter %1").arg(mode);
      CanvasPlanTarget::setImageFilter(mode, tint);
    }
    void commitLayoutLines(const stencil::core::Lines& next) override {
      log << QStringLiteral("rect");
      lines = next;
      CanvasPlanTarget::commitLayoutLines(next);
    }
    void answer(bool ok, const QString& err = QString()) {
      const OpDone d = std::move(pending);
      pending = nullptr;
      if (d) d(ok, err);
    }
  };

  struct Outcome {
    std::shared_ptr<std::optional<ScriptRunResult>> result =
        std::make_shared<std::optional<ScriptRunResult>>();
    std::function<void(const ScriptRunResult&)> sink() {
      return [result = result](const ScriptRunResult& r) { *result = r; };
    }
  };

  QImage fixtureImage() {
    QImage img;
    check(img.load(QStringLiteral(STENCIL_FIXTURES_DIR "/sample.png")), "fixture PNG loaded");
    return img;
  }

  void run(HeldTarget& target, Outcome& out) {
    stencil::gui::runScriptThen(stencil::model::ScriptDoc::parse(QString::fromUtf8(SCRIPT)),
                                target, out.sink());
  }

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);   // offscreen via QT_QPA_PLATFORM
  const stencil::core::PageSize a4{21.0, 29.7};

  std::printf("an open, a frame and later edits:\n");
  {
    HeldTarget target(fixtureImage(), a4);
    Outcome out;
    run(target, out);
    check(!out.result->has_value(), "the run returns parked at its @source");
    check(target.log == QStringList{"open https://example.com/a.png"}, "nothing past it has run");
    target.answer(true);
    check(!out.result->has_value(), "the @frame reload parks it again");
    check(target.log.size() == 2 && target.log.at(1) == QStringLiteral("frame 3"),
          "the frame is the block's own, and comes next");
    target.answer(true);
    check(out.result->has_value() && (*out.result)->isOk, "the last answer runs it to its end");
    check(out.result->has_value() && (*out.result)->ops == 4, "every op counted");
    check(target.log == QStringList({"open https://example.com/a.png", "frame 3", "filter bw", "rect"}),
          "in the script's order");
    const QImage img = target.renderResult();
    const QColor px = img.pixelColor(12, 10);
    check(target.lines.size() == 1 && px.red() == px.green() && px.green() == px.blue(),
          "the edits after the loads landed");
  }

  std::printf("a failed load:\n");
  {
    HeldTarget target(fixtureImage(), a4);
    Outcome out;
    run(target, out);
    target.answer(false, QStringLiteral("openUrl: timed out loading https://example.com/a.png"));
    check(out.result->has_value() && !(*out.result)->isOk && (*out.result)->line == 1,
          "a failed @source stops the run at its line");
    check(out.result->has_value() &&
              (*out.result)->error == QStringLiteral("openUrl: timed out loading https://example.com/a.png"),
          "…with the load's own reason");
    check(target.log.size() == 1, "…and nothing after it runs");
  }
  {
    HeldTarget target(fixtureImage(), a4);
    Outcome out;
    run(target, out);
    target.answer(false);
    check(out.result->has_value() &&
              (*out.result)->error == QStringLiteral("could not open 'https://example.com/a.png'"),
          "an @source failing without a reason names what it could not open");
  }
  {
    HeldTarget target(fixtureImage(), a4);
    Outcome out;
    run(target, out);
    target.answer(true);
    target.answer(false, QStringLiteral("frame: timed out loading https://example.com/a.png"));
    check(out.result->has_value() && (*out.result)->line == 2 && (*out.result)->ops == 1,
          "a failed @frame stops the run at its own line, the @source counted");
  }

  std::printf("a target torn down mid-load:\n");
  {
    Outcome out;
    {
      HeldTarget target(fixtureImage(), a4);
      run(target, out);
    }
    check(!out.result->has_value(), "the run ends with it, unanswered");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
