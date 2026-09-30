// §10 copyProject: an op that waits on the copy — the run resumes on its answer, a refused or
// reduced copy is a note and never a failed plan, and a surface without projects fails it.
#include "llmExecutorParts.hpp"

#include <functional>
#include <memory>
#include <optional>

using namespace stencil::llm;

namespace llmexec {

  namespace {

    struct CopyTarget : CanvasPlanTarget {
      using CanvasPlanTarget::CanvasPlanTarget;
      QStringList log;
      OpDone pending;
      void copyActiveProjectThen(const Action& a, OpDone done) override {
        log << QStringLiteral("copy %1 %2 %3 %4").arg(a.what, a.open, a.incognito ? "incognito" : "-",
                                                      a.local ? "local" : "-");
        pending = std::move(done);
      }
      void setImageFilter(const QString& mode, const QString& tint) override {
        log << QStringLiteral("filter %1").arg(mode);
        CanvasPlanTarget::setImageFilter(mode, tint);
      }
    };

    OpPlan copyPlan(const char* actions) {
      const auto parsed = parseOpPlan(QStringLiteral(R"({"reply":"r","actions":[%1]})")
                                          .arg(QString::fromUtf8(actions)));
      check(parsed.ok, "the copy plan parses");
      return parsed.plan;
    }

  }  // namespace

  void checkCopyOps(const QImage& img, const stencil::core::PageSize& a4) {
    std::printf("copyProject:\n");
    {
      CopyTarget target(img, a4);
      auto res = std::make_shared<std::optional<ExecResult>>();
      executePlanThen(copyPlan(R"({"op":"copyProject","what":"project","open":"here","incognito":true,"local":true},)"
                               R"({"op":"filter","mode":"bw"})"),
                      target, [res](const ExecResult& r) { *res = r; });
      check(!res->has_value() && target.log == QStringList({"copy project here incognito local"}),
            "the copy carries every field and holds the run until it answers");
      target.pending(true, QStringLiteral("a server copy cannot be incognito — made it on the server"));
      check(res->has_value() && (*res)->ok, "a reduced copy is not a failed plan");
      check(res->has_value() && (*res)->notes.contains(
                QStringLiteral("copyProject: a server copy cannot be incognito — made it on the server")),
            "…its note is surfaced");
      check(target.log.size() == 2 && target.log.last() == QStringLiteral("filter bw"),
            "the run resumes at the next action");
    }
    {
      CanvasPlanTarget bare(img, a4);
      const ExecResult res = executePlan(copyPlan(R"({"op":"copyProject","what":"image"})"), bare);
      check(!res.ok && res.error == QStringLiteral("copyProject: managing projects is not available here"),
            "a surface without projects fails the plan");
    }
    {
      OpPlan plan;
      Action copy;
      copy.op = OpKind::COPY_PROJECT;
      copy.what = QStringLiteral("layout");
      Variant v;
      v.actions << copy;
      plan.variants << v;
      CopyTarget target(img, a4);
      const ExecResult res = executePlan(plan, target);
      check(!res.ok && target.log.isEmpty(), "a copy inside a variant is refused before it starts");
    }
  }

}  // namespace llmexec
