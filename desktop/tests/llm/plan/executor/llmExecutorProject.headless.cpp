// Copy, project management and clearing the conversation.
#include "llmExecutorParts.hpp"

using namespace stencil::llm;

namespace llmexec {

  void checkProjectOps(const QImage& img, const stencil::core::PageSize& a4) {
  // ── §10 copy: the clipboard hand-off + the no-image note ──
  std::printf("copy:\n");
  {
    const auto parsed = parseOpPlan(R"({"reply":"c","actions":[{"op":"copy"}]})");
    check(parsed.ok, "copy plan parses");
    {
      // With a working image the target's clipboard path runs.
      CanvasPlanTarget target(img, a4);
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok && target.copied && res.notes.isEmpty(),
            "copy reaches the target's clipboard path");
    }
    {
      // Copying nothing is a skipped action + note, never a failed plan (§10).
      CanvasPlanTarget target(QImage(), a4);
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok && !target.copied, "copy with no image does not fail the plan");
      check(res.notes.size() == 1 && res.notes.first().contains("no working image"),
            "…it lands as a skipped-copy note");
    }
    {
      // The base PlanTarget has no clipboard — a typed failure, not a crash.
      struct BareTarget : CanvasPlanTarget {
        using CanvasPlanTarget::CanvasPlanTarget;
        bool copyImage(QString* err) override { return PlanTarget::copyImage(err); }
      };
      BareTarget target(img, a4);
      const ExecResult res = executePlan(parsed.plan, target);
      check(!res.ok && res.error.contains("copy"), "clipboard-less target rejects copy");
    }
  }

  // ── §10 project management: notes surface, defaults reject ──
  std::printf("project management:\n");
  {
    // The injected flows run in plan order; a non-empty note (unknown name,
    // declined confirm, empty store) surfaces without failing the plan.
    struct ProjectsTarget : CanvasPlanTarget {
      using CanvasPlanTarget::CanvasPlanTarget;
      QStringList calls;
      QString removeNote, clearNote;   // "" = success (nothing to report)
      bool removeProjectNamed(const QString& name, bool current, QString* note) override {
        calls << (current ? QStringLiteral("remove:<current>")
                          : QStringLiteral("remove:%1").arg(name));
        *note = removeNote;
        return true;
      }
      bool clearProjects(bool keepCurrent, QString* note) override {
        calls << (keepCurrent ? QStringLiteral("clear:<others>") : QStringLiteral("clear"));
        *note = clearNote;
        return true;
      }
    };
    const auto parsed = parseOpPlan(
        R"({"reply":"p","actions":[{"op":"removeProject","name":"a"},{"op":"clearProjects"}]})");
    check(parsed.ok, "project-management plan parses");
    {
      ProjectsTarget target(img, a4);
      target.removeNote = QStringLiteral("removal canceled");
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok && target.calls == QStringList({"remove:a", "clear"}),
            "both ops reach the target's flows, in order");
      check(res.notes.size() == 1 &&
                res.notes.first() == "removeProject: removal canceled",
            "a declined remove lands as a note, never a failed plan");
    }
    {
      ProjectsTarget target(img, a4);
      target.clearNote = QStringLiteral("no saved projects to clear");
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok && res.notes.size() == 1 &&
                res.notes.first() == "clearProjects: no saved projects to clear",
            "an empty store lands as a clearProjects note");
    }
    {
      // The base PlanTarget cannot manage projects — a typed failure each.
      CanvasPlanTarget target(img, a4);
      const ExecResult r1 = executePlan(
          parseOpPlan(R"({"reply":"p","actions":[{"op":"removeProject","name":"a"}]})").plan,
          target);
      check(!r1.ok && r1.error.contains("removeProject") &&
                r1.error.contains("not available here"),
            "default target rejects removeProject");
      const ExecResult r2 = executePlan(
          parseOpPlan(R"({"reply":"p","actions":[{"op":"clearProjects"}]})").plan, target);
      check(!r2.ok && r2.error.contains("clearProjects") &&
                r2.error.contains("not available here"),
            "default target rejects clearProjects");
    }
  }

  // ── §10 clearChat: deferred to the plan's end, three-valued, scope-banned ──
  std::printf("clearChat (s10):\n");
  {
    // Records the order the flows run in: clearChat is listed FIRST but must
    // reach its hook LAST (contract §10 end-of-turn deferral).
    struct ChatTarget : CanvasPlanTarget {
      using CanvasPlanTarget::CanvasPlanTarget;
      QStringList calls;
      QString chatNote;   // "" = cleared (nothing to report)
      void setUnits(const QString& v) override {
        CanvasPlanTarget::setUnits(v);
        calls << QStringLiteral("units");
      }
      bool clearChat(QString* note) override {
        calls << QStringLiteral("clearChat");
        *note = chatNote;
        return true;
      }
    };
    const auto parsed = parseOpPlan(
        R"({"reply":"c","actions":[{"op":"clearChat"},{"op":"units","value":"in"}]})");
    check(parsed.ok, "clearChat plan parses");
    {
      ChatTarget target(img, a4);
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok && res.changed, "clearChat plan executes");
      check(target.calls == QStringList({"units", "clearChat"}),
            "clearChat runs LAST even when listed first");
      check(res.notes.isEmpty(), "a clean clear reports nothing");
    }
    {
      // Declined confirm: a note, never a failed plan (the other action ran).
      ChatTarget target(img, a4);
      target.chatNote = QStringLiteral("clear canceled");
      const ExecResult res = executePlan(parsed.plan, target);
      check(res.ok && res.notes == QStringList({"clearChat: clear canceled"}),
            "a declined confirm lands as a clearChat note");
      check(target.unitsValue == "in", "the plan's other action still ran");
    }
    {
      // The plain sandbox target records the request; the base target rejects.
      CanvasPlanTarget target(img, a4);
      const ExecResult res = executePlan(
          parseOpPlan(R"({"reply":"c","actions":[{"op":"clearChat"}]})").plan, target);
      check(res.ok && target.chatCleared, "CanvasPlanTarget records the deferred clear");
      struct BareTarget : CanvasPlanTarget {
        using CanvasPlanTarget::CanvasPlanTarget;
        bool clearChat(QString* note) override { return PlanTarget::clearChat(note); }
      };
      BareTarget bare(img, a4);
      const ExecResult r2 = executePlan(
          parseOpPlan(R"({"reply":"c","actions":[{"op":"clearChat"}]})").plan, bare);
      check(!r2.ok && r2.error.contains("clearChat") &&
                r2.error.contains("no conversation"),
            "chat-less base target rejects clearChat");
    }
    // Strict fields + the variant / ask-preview scope (§1: the variant/preview
    // is dropped with a warning, the plan lives).
    check(!parseOpPlan(
               R"({"reply":"c","actions":[{"op":"clearChat","scope":"all"}]})").ok,
          "clearChat takes no fields");
    {
      const OpPlanResult v = parseOpPlan(R"({"reply":"c","variants":[{"label":"v","actions":[)"
                                         R"({"op":"clearChat"}]}]})");
      check(v.ok && v.plan.variants.isEmpty() &&
                v.plan.warnings.value(0).startsWith("Dropped variant"),
            "clearChat inside a variant drops the variant");
      const OpPlanResult r = parseOpPlan(
          R"({"reply":"c","ask":{"question":"q?","options":[)"
          R"({"label":"a","actions":[{"op":"clearChat"}]},{"label":"b"}]}})");
      check(r.ok && r.plan.ask.options.size() == 2 &&
                r.plan.ask.options[0].actions.isEmpty(),
            "clearChat inside an ask-option preview drops the preview");
    }
    check(isEditorSettingsOp(OpKind::CLEAR_CHAT) && isTopLevelOnlyOp(OpKind::CLEAR_CHAT),
          "clearChat is editor-settings scoped and top-level only");
  }

  }

}  // namespace llmexec
