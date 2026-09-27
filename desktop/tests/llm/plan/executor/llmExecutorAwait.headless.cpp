// The run across an op that waits on I/O: executePlanThen returns with the plan suspended there, the
// op's answer resumes it at the next action, and a failed answer ends it as a synchronous failure did.
#include "llmExecutorParts.hpp"

#include <functional>
#include <memory>
#include <optional>

using namespace stencil::llm;

namespace llmexec {

  namespace {

    constexpr char URL[] = "https://a.example/cat.png";

    // Holds every awaited op's answer until the test gives it; logs the order the ops ran in.
    struct AwaitTarget : CanvasPlanTarget {
      using CanvasPlanTarget::CanvasPlanTarget;
      QStringList log;
      OpDone pending;
      bool answerAtOnce = false;
      stencil::core::Lines got;
      QString userTypedText() const override { return QStringLiteral("open %1").arg(URL); }
      void openUrlThen(const QString&, bool, OpDone done) override {
        log << QStringLiteral("openUrl");
        if (answerAtOnce) return done(true, QString());
        pending = std::move(done);
      }
      void connectServerThen(const QString&, OpDone done) override {
        log << QStringLiteral("connect");
        pending = std::move(done);
      }
      void setImageFilter(const QString& mode, const QString& tint) override {
        log << QStringLiteral("filter %1").arg(mode);
        CanvasPlanTarget::setImageFilter(mode, tint);
      }
      void setLayoutLines(const stencil::core::Lines& lines) override {
        log << QStringLiteral("layout");
        got = lines;
        CanvasPlanTarget::setLayoutLines(lines);
      }
      bool openDialog(const QString&, QString*) override {
        log << QStringLiteral("dialog");
        return true;
      }
      bool clearChat(QString*) override {
        log << QStringLiteral("clearChat");
        return true;
      }
      void answer(bool ok, const QString& err = QString()) {
        const OpDone d = std::move(pending);
        pending = nullptr;
        if (d) d(ok, err);
      }
    };

    OpPlan planOf(const char* actions) {
      const auto parsed = parseOpPlan(QStringLiteral(R"({"reply":"r","actions":[%1]})")
                                          .arg(QString::fromUtf8(actions)));
      check(parsed.ok, "the plan parses");
      return parsed.plan;
    }

    struct Outcome {
      std::shared_ptr<std::optional<ExecResult>> res =
          std::make_shared<std::optional<ExecResult>>();
      std::function<void(const ExecResult&)> sink() {
        return [res = res](const ExecResult& r) { *res = r; };
      }
    };

  }  // namespace

  void checkAwaitedOps(const QImage& img, const stencil::core::PageSize& a4) {
    std::printf("awaited ops (continuations):\n");
    {
      AwaitTarget target(img, a4);
      Outcome out;
      executePlanThen(planOf(R"({"op":"clearChat"},{"op":"filter","mode":"bw"},)"
                             R"({"op":"openUrl","url":"https://a.example/cat.png"},)"
                             R"({"op":"dialog","name":"servers"},{"op":"filter","mode":"sepia"})"),
                      target, out.sink());
      check(!out.res->has_value(), "the run returns unanswered while an op awaits");
      check(target.log == QStringList({"filter bw", "openUrl"}),
            "nothing after the awaited op runs before its answer");
      target.answer(true);
      check(out.res->has_value() && (*out.res)->ok && (*out.res)->changed,
            "the answer resumes the run to its end");
      check(target.log == QStringList({"filter bw", "openUrl", "filter sepia", "dialog", "clearChat"}),
            "the rest runs in order, the deferred dialog and clearChat last");
    }
    {
      AwaitTarget target(img, a4);
      Outcome out;
      executePlanThen(planOf(R"({"op":"openUrl","url":"https://a.example/cat.png"},)"
                             R"({"op":"filter","mode":"bw"})"),
                      target, out.sink());
      target.answer(false, QStringLiteral("openUrl: timed out loading x"));
      check(out.res->has_value() && !(*out.res)->ok, "a failed answer fails the plan");
      check(out.res->has_value() && (*out.res)->error == QStringLiteral("openUrl: timed out loading x"),
            "…with the op's own error");
      check(target.log == QStringList({"openUrl"}), "…and stops it there");
    }
    {
      AwaitTarget target(img, a4);
      target.answerAtOnce = true;
      Outcome out;
      executePlanThen(planOf(R"({"op":"openUrl","url":"https://a.example/cat.png"},)"
                             R"({"op":"filter","mode":"bw"})"),
                      target, out.sink());
      check(out.res->has_value() && (*out.res)->ok, "an answer given at once runs the plan through");
      check(target.log == QStringList({"openUrl", "filter bw"}), "…inside the same call");
    }
    {
      // A loaded picture is a fresh frame; a connection is not, so the crop still maps the layout.
      const char* layout = R"({"op":"layout","lines":[{"points":[{"x":5,"y":4},{"x":8,"y":6}]}]})";
      AwaitTarget loads(img, a4);
      executePlanThen(planOf(QStringLiteral(R"({"op":"crop","spec":{"x1":"2px","y1":"2px","x2":"14px","y2":"10px"}},)"
                                            R"({"op":"openUrl","url":"https://a.example/cat.png"},%1)")
                                 .arg(QString::fromUtf8(layout)).toUtf8().constData()),
                      loads, Outcome().sink());
      loads.answer(true);
      AwaitTarget connects(img, a4);
      executePlanThen(planOf(QStringLiteral(R"({"op":"crop","spec":{"x1":"2px","y1":"2px","x2":"14px","y2":"10px"}},)"
                                            R"({"op":"connect","server":"a.example"},%1)")
                                 .arg(QString::fromUtf8(layout)).toUtf8().constData()),
                      connects, Outcome().sink());
      connects.answer(true);
      check(loads.got.size() == 1 && near(loads.got[0].points[0].x, 5),
            "after openUrl the layout lands in the new picture's own frame");
      check(connects.got.size() == 1 && near(connects.got[0].points[0].x, 3),
            "after connect the earlier crop still maps it");
    }
    {
      // A variant refuses an awaited op before it could start.
      OpPlan plan;
      Action open;
      open.op = OpKind::OPEN_URL;
      open.url = QString::fromUtf8(URL);
      Variant v;
      v.actions << open;
      plan.variants << v;
      AwaitTarget target(img, a4);
      const ExecResult res = executePlan(plan, target);
      check(!res.ok && res.error == QStringLiteral("variant 1: openUrl: not allowed inside a variant"),
            "an awaited op inside a variant is refused");
      check(target.log.isEmpty(), "…and never reaches the target");
    }
    {
      // A target torn down mid-await takes the run with it: no answer, nothing dangling.
      Outcome out;
      {
        AwaitTarget target(img, a4);
        executePlanThen(planOf(R"({"op":"openUrl","url":"https://a.example/cat.png"})"), target,
                        out.sink());
      }
      check(!out.res->has_value(), "a run whose target is gone is never answered");
    }
  }

}  // namespace llmexec
