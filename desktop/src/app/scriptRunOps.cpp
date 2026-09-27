// The .stc ops that apply at once, each onto the SAME PlanTarget the assistant's op plans drive:
// crop, filter, the shapes, undo and save (contracts/stc §3, §7).
#include "scriptRunParts.hpp"

namespace stencil::gui::scriptrun {

  using model::ScriptDoc;
  using model::ScriptOp;
  using model::ScriptOpKind;

  bool isEdit(ScriptOpKind kind) {
    return kind == ScriptOpKind::CROP || kind == ScriptOpKind::FILTER ||
           kind == ScriptOpKind::LINE || kind == ScriptOpKind::RECT ||
           kind == ScriptOpKind::LAYOUT;
  }

  void fail(RunState& r, const QString& message, const ScriptOp& op) {
    r.out.isOk = false;
    r.out.error = message;
    r.out.line = op.line;
    r.out.col = op.col;
  }

  QString strAt(const ScriptOp& op, int i) {
    return i < op.strs.size() ? op.strs[i] : QString();
  }

  namespace {

    bool runShape(RunState& r, const ScriptOp& op) {
      const std::optional<core::EditorMemento> now = r.target.captureEdit();
      core::Lines lines = now ? now->lines : core::Lines{};
      bool resolved = false;
      ScriptDoc::appendLine(lines, op, r.target.workingSize(), &resolved);
      if (!resolved) {
        fail(r, QStringLiteral("this shape resolves to nothing"), op);
        return false;
      }
      r.target.commitLayoutLines(lines);   // append + ONE undo step
      return true;
    }

    // §7: `undo N` unwinds the last N applied edits, so the checkpoint before the earliest of
    // them is what stands. The lowerer replays the survivors as ordinary ops after it.
    void runUndo(RunState& r, const ScriptOp& op) {
      const int steps = op.nums.isEmpty() ? 1 : static_cast<int>(op.nums[0]);
      std::optional<core::EditorMemento> back;
      for (int done = 0; done < steps && !r.marks.isEmpty(); ++done) back = r.marks.takeLast();
      if (back) r.target.restoreEdit(*back);
    }

  }  // namespace

  bool runOp(RunState& r, const ScriptOp& op) {
    QString err;
    switch (op.kind) {
      case ScriptOpKind::OPEN:
      case ScriptOpKind::FRAME:
        return true;   // they wait on I/O, so the run starts them itself
      case ScriptOpKind::CROP: {
        bool resolved = false;
        const auto rect = ScriptDoc::cropRect(op, r.target.workingSize(), &resolved);
        if (!resolved) { fail(r, QStringLiteral("this crop resolves to nothing"), op); return false; }
        if (!r.target.applyCropRect(rect)) { fail(r, QStringLiteral("the crop was refused"), op); return false; }
        return true;
      }
      case ScriptOpKind::FILTER: {
        const QString mode = strAt(op, 0);
        r.target.setImageFilter(mode, mode == QStringLiteral("custom") ? strAt(op, 1) : QString());
        return true;
      }
      case ScriptOpKind::LINE:
      case ScriptOpKind::RECT:
        return runShape(r, op);
      case ScriptOpKind::LAYOUT:
        fail(r, QStringLiteral("@layout needs a file the app can read — open it instead"), op);
        return false;
      case ScriptOpKind::UNDO:
        runUndo(r, op);
        return true;
      case ScriptOpKind::REDO:
        return true;   // a redo never reaches an adapter: the lowerer resolved it (§7)
      case ScriptOpKind::SAVE: {
        const QString name = strAt(op, 0);
        if (!r.target.saveProject(name, QString(), &err)) { fail(r, err, op); return false; }
        return true;
      }
    }
    return true;
  }

}  // namespace stencil::gui::scriptrun
