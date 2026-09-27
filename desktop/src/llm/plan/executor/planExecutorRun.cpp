// A validated op plan against a PlanTarget (llm-contract §1): every action in order, stopping at the
// first failure. An op that waits on I/O suspends the run, and its answer resumes it at the next action.
#include "planExecutorParts.hpp"

#include "opRegistry.hpp"

#include <memory>
#include <optional>

namespace stencil::llm {

  using namespace exec;

  namespace exec {

    void applyActionThen(const Action& a, PlanTarget& target, FrameMap& frame, bool inVariant,
                         QStringList* notes, QString* err, const std::function<void(bool)>& done) {
      // §13 forbidden-op tooth: no registered op uses a forbidden name (a test
      // pins that), but reject defensively even if one somehow appears.
      if (rejectForbiddenOp(opName(a.op), err)) return done(false);
      if (isAwaitedOp(a.op)) return startAwaitedAction(a, target, frame, inVariant, notes, err, done);
      bool handled = false;
      bool ok = applyImageAction(a, target, frame, inVariant, notes, &handled, err);
      if (!handled) ok = applyEditAction(a, target, frame, inVariant, notes, &handled, err);
      if (!handled) ok = applyStateAction(a, target, frame, inVariant, notes, &handled, err);
      if (!handled) {
        *err = QStringLiteral("unhandled op");
        ok = false;
      }
      done(ok);
    }

  }  // namespace exec

  namespace {

    // One run of a plan. The action that awaits holds it through its answer's callback, so a
    // run whose target is torn down mid-await ends with it, unanswered.
    struct PlanRun : std::enable_shared_from_this<PlanRun> {
      PlanRun(const OpPlan& plan, PlanTarget& target, std::function<void(const ExecResult&)> done)
          : plan(plan), target(target), done(std::move(done)) {}

      // Runs actions from `next` until one awaits; its answer comes back through landed().
      void step() {
        while (next < plan.actions.size()) {
          const Action& a = plan.actions.at(next++);
          // §10 clearChat is DEFERRED to the end of the plan — wherever the model
          // put it, every other action (and the variants) runs first.
          if (a.op == OpKind::CLEAR_CHAT) { clearChatLast = true; continue; }
          if (a.op == OpKind::DIALOG) { dialogLast = a; hasDialog = true; continue; }
          const auto answer = std::make_shared<Answer>();
          applyActionThen(a, target, frame, /*inVariant=*/false, &res.notes, &err,
                          [self = shared_from_this(), answer](bool ok) {
                            answer->given = true;
                            answer->ok = ok;
                            if (answer->waiting) self->landed(ok);
                          });
          if (!answer->given) {
            answer->waiting = true;
            return;
          }
          if (!answer->ok) return fail();
          res.changed = true;
        }
        finish();
      }

      void landed(bool ok) {
        if (!ok) return fail();
        res.changed = true;
        step();
      }

      void fail() {
        res.error = err;
        respond();
      }

      void finish() {
        if (!plan.variants.isEmpty() && !runVariants()) return respond();
        if (hasDialog) {
          // §10: a window in front of the user is the LAST thing a turn does — the edits
          // and the reply land first, then the dialog (browser row/plan.js `deferred`).
          QString note;
          if (!target.openDialog(dialogLast.current ? QString() : dialogLast.dialog, &note)) {
            res.error = note;
            return respond();
          }
          if (!note.isEmpty()) res.notes << QStringLiteral("dialog: %1").arg(note);
          res.changed = true;
        }
        if (clearChatLast) {
          // §10: the surface's clear flow (confirm included) runs last; a note
          // (declined confirm) surfaces without failing the plan.
          QString note;
          if (!target.clearChat(&note)) {
            res.error = note;
            return respond();
          }
          if (!note.isEmpty()) res.notes << QStringLiteral("clearChat: %1").arg(note);
          res.changed = true;
        }
        res.ok = true;
        respond();
      }

      // Each variant branches from the image AFTER the top-level actions, in a sandbox that never
      // awaits (a variant refuses the ops that would), so every answer here comes at once.
      bool runVariants() {
        const QImage snapshot = target.renderResult();
        if (snapshot.isNull()) {
          res.error = QStringLiteral("variants need a working image");
          return false;
        }
        for (int i = 0; i < plan.variants.size(); ++i) {
          const Variant& v = plan.variants.at(i);
          CanvasPlanTarget sandbox(snapshot, target.pageCm());
          // Variant coordinates are model-frame too — start from the mapping the
          // top-level actions accumulated (the state the variant branches from).
          FrameMap vframe = frame;
          for (const Action& a : v.actions) {
            bool ok = false;
            applyActionThen(a, sandbox, vframe, /*inVariant=*/true, &res.notes, &err,
                            [&ok](bool answered) { ok = answered; });
            if (!ok) {
              res.error = QStringLiteral("variant %1: %2").arg(i + 1).arg(err);
              return false;
            }
          }
          QString label = sanitizeLabel(v.label);
          if (label.isEmpty()) label = QStringLiteral("variant %1").arg(i + 1);
          res.variants.append({label, sandbox.renderResult()});
        }
        return true;
      }

      void respond() {
        const auto answerTo = std::move(done);
        done = nullptr;
        if (answerTo) answerTo(res);
      }

      struct Answer {
        bool given = false, ok = false, waiting = false;
      };

      const OpPlan plan;
      PlanTarget& target;
      std::function<void(const ExecResult&)> done;
      ExecResult res;
      QString err;
      FrameMap frame;  // model frame → working frame (contract §1)
      qsizetype next = 0;
      bool clearChatLast = false;
      // §10 dialog: modal, so it opens once the plan is done — and only the LAST one asked
      // for ("close this and open that" ends with that one open; browser flushDeferred).
      Action dialogLast;
      bool hasDialog = false;
    };

  }  // namespace

  void executePlanThen(const OpPlan& plan, PlanTarget& target,
                       std::function<void(const ExecResult&)> done) {
    std::make_shared<PlanRun>(plan, target, std::move(done))->step();
  }

  ExecResult executePlan(const OpPlan& plan, PlanTarget& target) {
    const auto out = std::make_shared<std::optional<ExecResult>>();
    executePlanThen(plan, target, [out](const ExecResult& res) { *out = res; });
    Q_ASSERT_X(out->has_value(), "executePlan", "an op awaited: use executePlanThen");
    return out->value_or(ExecResult{});
  }

}  // namespace stencil::llm
