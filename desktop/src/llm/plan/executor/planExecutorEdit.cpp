// The ops that move through the editor's own history and sources: undo/redo and an image reference.
// The frame, URL, file and save ops wait on I/O, so they start from planExecutorAwait.cpp.
#include "planExecutorParts.hpp"

namespace stencil::llm::exec {

  bool applyEditAction(const Action& a, PlanTarget& target, FrameMap& frame, bool inVariant,
                       QStringList* notes, bool* handled, QString* err) {
    *handled = true;
    switch (a.op) {
        case OpKind::UNDO:
        case OpKind::REDO: {
          const bool redo = a.op == OpKind::REDO;
          const char* name = redo ? "redo" : "undo";
          if (inVariant) {  // parse-banned; defensive only
            *err = QStringLiteral("%1: not allowed inside a variant").arg(QLatin1String(name));
            return false;
          }
          const int done = target.stepHistory(redo, a.steps);
          if (done < 0) {
            *err = QStringLiteral("%1: edit history is not available here")
                       .arg(QLatin1String(name));
            return false;
          }
          // §2: one step is one HISTORY entry — say so when steps run out.
          if (done < a.steps && notes)
            *notes << (done == 0
                           ? QStringLiteral("%1: nothing to %1").arg(QLatin1String(name))
                           : QStringLiteral("%1: only %2 of %3 step(s) available")
                                 .arg(QLatin1String(name))
                                 .arg(done)
                                 .arg(a.steps));
          // History steps rebuild earlier image states — the model-frame map
          // no longer describes them.
          if (done > 0) frame.reset();
          return true;
        }
        // §2.1 multi-image ops (parse-banned in variants; the guards here
        // are defensive only)
        case OpKind::IMAGE: {
          if (inVariant) {
            *err = QStringLiteral("image: not allowed inside a variant");
            return false;
          }
          // An index this turn cannot satisfy costs the ACTION, not the plan.
          QString why;
          if (!target.loadAttachment(a.index, &why) && notes)
            *notes << QStringLiteral("Skipped switching to attached image %1 — %2")
                          .arg(a.index)
                          .arg(why);
          frame.reset();  // the attachment is a fresh image, so a fresh frame
          return true;
        }
      default: break;
    }
    *handled = false;
    return false;
  }

}  // namespace stencil::llm::exec
