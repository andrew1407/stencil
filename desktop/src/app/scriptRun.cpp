// The .stc runner: each lowered op onto the PlanTarget, in order. An @source or @frame load waits
// on I/O, so it suspends the run, and its answer resumes it at the next op (contracts/stc §3, §10).
#include "scriptRunParts.hpp"

#include <QFile>

#include <memory>
#include <optional>

namespace stencil::gui {

  namespace {

    using namespace scriptrun;
    using model::ScriptBlock;
    using model::ScriptOp;
    using model::ScriptOpKind;

    // The action that awaits holds the run through its answer's callback, so a run whose
    // target is torn down mid-load ends with it, unanswered.
    struct Run : RunState, std::enable_shared_from_this<Run> {
      Run(const model::ScriptDoc& program, llm::PlanTarget& target,
          std::function<void(const ScriptRunResult&)> done, ScriptRunRules rules)
          : RunState{program, target, {}, {}, rules}, done(std::move(done)) {}

      void step() {
        const QVector<ScriptOp>& ops = program.getOps();
        while (next < ops.size()) {
          const ScriptOp& op = ops.at(next++);
          const auto mark = isEdit(op.kind) ? target.captureEdit() : std::nullopt;
          if (mark) marks.push_back(*mark);
          if (op.kind == ScriptOpKind::OPEN || op.kind == ScriptOpKind::FRAME) {
            const auto answer = std::make_shared<Answer>();
            startLoad(op, [self = shared_from_this(), answer, &op](bool ok, const QString& err) {
              answer->given = true;
              answer->ok = ok;
              answer->err = err;
              if (answer->waiting) self->resume(op, ok, err);
            });
            if (!answer->given) {
              answer->waiting = true;
              return;
            }
            if (!loaded(op, answer->ok, answer->err)) return respond();
          } else if (!runOp(*this, op)) {
            return respond();
          }
          ++out.ops;
        }
        respond();
      }

      void startLoad(const ScriptOp& op, llm::OpDone done) {
        if (op.kind == ScriptOpKind::OPEN) {
          const QString spec = strAt(op, 0);
          if (isWebUrl(spec)) return target.openUrlThen(spec, false, std::move(done));
          if (rules.webSourcesOnly) return done(false, localRefusal(spec));
          return target.openFileThen(spec, std::move(done));
        }
        // The block's own source, reloaded at that frame — not the chat's frames-as-projects.
        const QVector<ScriptBlock>& blocks = program.getBlocks();
        const ScriptBlock block = op.block >= 0 && op.block < blocks.size() ? blocks.at(op.block)
                                                                           : ScriptBlock{};
        const int frame = op.nums.isEmpty() ? block.frame : static_cast<int>(op.nums[0]);
        if (rules.webSourcesOnly && !isWebUrl(block.source)) return done(false, localRefusal(block.source));
        target.openSourceFrameThen(block.source, frame, std::move(done));
      }

      static bool isWebUrl(const QString& spec) {
        return spec.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
            || spec.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive);
      }

      static QString localRefusal(const QString& spec) {
        return QStringLiteral("a script from a link may open web images only, not '%1'").arg(spec);
      }

      // A fresh picture starts a fresh set of numbered edits.
      bool loaded(const ScriptOp& op, bool ok, const QString& err) {
        if (!ok) {
          const bool open = op.kind == ScriptOpKind::OPEN;
          fail(*this, open && err.isEmpty() ? QStringLiteral("could not open '%1'").arg(strAt(op, 0))
                                            : err,
               op);
          return false;
        }
        marks.clear();
        return true;
      }

      void resume(const ScriptOp& op, bool ok, const QString& err) {
        if (!loaded(op, ok, err)) return respond();
        ++out.ops;
        step();
      }

      void respond() {
        const auto answerTo = std::move(done);
        done = nullptr;
        if (answerTo) answerTo(out);
      }

      struct Answer {
        bool given = false, ok = false, waiting = false;
        QString err;
      };

      std::function<void(const ScriptRunResult&)> done;
      qsizetype next = 0;
    };

    ScriptRunResult answeredAtOnce(
        const std::function<void(std::function<void(const ScriptRunResult&)>)>& start) {
      const auto out = std::make_shared<std::optional<ScriptRunResult>>();
      start([out](const ScriptRunResult& result) { *out = result; });
      Q_ASSERT_X(out->has_value(), "runScript", "a load awaited: use runScriptThen");
      return out->value_or(ScriptRunResult{});
    }

  }  // namespace

  void runScriptThen(const model::ScriptDoc& program, llm::PlanTarget& target,
                     std::function<void(const ScriptRunResult&)> done, ScriptRunRules rules) {
    ScriptRunResult out;
    for (const model::ScriptDiagnostic& d : program.getDiagnostics()) {
      if (!d.isError) continue;
      out.isOk = false;
      out.error = d.message;
      out.line = d.line;
      out.col = d.col;
      return done(out);   // an erroring script runs nothing at all
    }
    const auto& ops = program.getOps();
    const bool bringsItsOwn = !ops.isEmpty() && ops.front().kind == ScriptOpKind::OPEN;
    if (!bringsItsOwn && !target.hasImage()) {
      out.isOk = false;
      out.error = QStringLiteral("open an image first");
      return done(out);
    }
    std::make_shared<Run>(program, target, std::move(done), rules)->step();
  }

  void runScriptFileThen(const QString& path, llm::PlanTarget& target,
                         std::function<void(const ScriptRunResult&)> done) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      ScriptRunResult out;
      out.isOk = false;
      out.error = QStringLiteral("could not read '%1'").arg(path);
      return done(out);
    }
    runScriptThen(model::ScriptDoc::parse(QString::fromUtf8(file.readAll())), target,
                  std::move(done));
  }

  ScriptRunResult runScript(const model::ScriptDoc& program, llm::PlanTarget& target) {
    return answeredAtOnce([&](auto done) { runScriptThen(program, target, std::move(done)); });
  }

  ScriptRunResult runScript(const QString& text, llm::PlanTarget& target) {
    return runScript(model::ScriptDoc::parse(text), target);
  }

  ScriptRunResult runScriptFile(const QString& path, llm::PlanTarget& target) {
    return answeredAtOnce([&](auto done) { runScriptFileThen(path, target, std::move(done)); });
  }

}  // namespace stencil::gui
