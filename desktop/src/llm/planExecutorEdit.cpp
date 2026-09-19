// The ops that move through the editor's own history and sources: undo/redo, a video frame, a URL
// or file to open, an image reference and a save.
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
        case OpKind::FRAME: {
          if (inVariant) {
            *err = QStringLiteral("frame: not valid inside a variant");
            return false;
          }
          if (!target.isVideoInput()) {
            *err = QStringLiteral("frame: the current input is not a video");
            return false;
          }
          if (!target.extractFrames(a.indices, err)) return false;
          frame.reset();
          return true;
        }
        case OpKind::OPEN_URL: {
          if (inVariant) {
            *err = QStringLiteral("openUrl: not allowed inside a variant");
            return false;
          }
          // The model may only ECHO the user: the exact URL must appear in the user's own messages this
          // conversation (the `connect` stance - a plan can never introduce a host).
          if (!target.userTypedText().contains(a.url)) {
            *err = QStringLiteral(
                       "openUrl blocked: \"%1\" is not a URL you gave in this conversation")
                       .arg(a.url);
            return false;
          }
          if (!target.openUrl(a.url, a.incognito, err)) return false;
          frame.reset();  // the loaded picture is a fresh frame
          return true;
        }
        case OpKind::OPEN_FILE: {
          if (inVariant) {
            *err = QStringLiteral("openFile: not allowed inside a variant");
            return false;
          }
          // The same echo rule as openUrl, for the filesystem: the assistant reads
          // only where the user themselves pointed it.
          if (!pathEchoedIn(target.userTypedText(), a.path)) {
            *err = QStringLiteral(
                       "openFile blocked: \"%1\" is not a path you gave in this conversation")
                       .arg(a.path);
            return false;
          }
          if (!target.openFile(a.path, err)) return false;
          frame.reset();  // the loaded picture is a fresh frame
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
        case OpKind::SAVE: {
          if (inVariant) {
            *err = QStringLiteral("save: not allowed inside a variant");
            return false;
          }
          // Saving nothing is a skipped action, never a failed plan.
          if (!target.hasImage()) {
            if (notes) *notes << QStringLiteral("Skipped save — no working image to save");
            return true;
          }
          // A destination is honoured only when the user wrote it (§10's echo rule);
          // an unechoed one costs the destination, not the save.
          QString dest = a.path;
          if (!dest.isEmpty() && !pathEchoedIn(target.userTypedText(), dest)) {
            if (notes)
              *notes << QStringLiteral("Saved to the usual place — \"%1\" is not a path you "
                                       "gave in this conversation")
                            .arg(dest);
            dest.clear();
          }
          return target.saveProject(a.name, dest, err);
        }
      default: break;
    }
    *handled = false;
    return false;
  }

}  // namespace stencil::llm::exec
