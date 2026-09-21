#include "scriptRun.hpp"

#include "ScriptDoc.hpp"
#include "planExecutor.hpp"

#include <QFile>
#include <QVector>

namespace stencil::gui {

  namespace {

    using model::ScriptBlock;
    using model::ScriptDoc;
    using model::ScriptOp;
    using model::ScriptOpKind;

    /* One run. `marks` is a checkpoint per applied edit, newest last: the canvas history holds
     * committed lines only, so an `undo` that crosses a @crop or @filter reverts from here
     * (contracts/stc §7). A failure part-way keeps whatever already ran. */
    struct Run {
      const ScriptDoc& program;
      llm::PlanTarget& target;
      QVector<llm::EditState> marks;
      ScriptRunResult out;
    };

    // The numbered edits of §7; @frame and @source start a fresh set.
    bool isEdit(ScriptOpKind kind) {
      return kind == ScriptOpKind::CROP || kind == ScriptOpKind::FILTER ||
             kind == ScriptOpKind::LINE || kind == ScriptOpKind::RECT ||
             kind == ScriptOpKind::LAYOUT;
    }

    void fail(Run& r, const QString& message, const ScriptOp& op) {
      r.out.isOk = false;
      r.out.error = message;
      r.out.line = op.line;
      r.out.col = op.col;
    }

    QString strAt(const ScriptOp& op, int i) {
      return i < op.strs.size() ? op.strs[i] : QString();
    }

    bool runOpen(Run& r, const ScriptOp& op) {
      QString err;
      const QString spec = strAt(op, 0);
      const bool isUrl = spec.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
                      || spec.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive);
      const bool opened = isUrl ? r.target.openUrl(spec, false, &err)
                                : r.target.openFile(spec, &err);
      if (!opened) {
        fail(r, err.isEmpty() ? QStringLiteral("could not open '%1'").arg(spec) : err, op);
        return false;
      }
      r.marks.clear();
      return true;
    }

    // The block's own source, reloaded at that frame — not the chat's frames-as-projects.
    bool runFrame(Run& r, const ScriptOp& op) {
      const QVector<ScriptBlock>& blocks = r.program.getBlocks();
      const ScriptBlock block = op.block >= 0 && op.block < blocks.size() ? blocks.at(op.block)
                                                                         : ScriptBlock{};
      const int frame = op.nums.isEmpty() ? block.frame : static_cast<int>(op.nums[0]);
      QString err;
      if (!r.target.openSourceFrame(block.source, frame, &err)) {
        fail(r, err, op);
        return false;
      }
      r.marks.clear();
      return true;
    }

    bool runShape(Run& r, const ScriptOp& op) {
      llm::EditState state;
      r.target.captureEdit(state);
      bool resolved = false;
      ScriptDoc::appendLine(state.lines, op, r.target.workingSize(), &resolved);
      if (!resolved) {
        fail(r, QStringLiteral("this shape resolves to nothing"), op);
        return false;
      }
      r.target.commitLayoutLines(state.lines);   // append + ONE undo step
      return true;
    }

    // §7: `undo N` unwinds the last N applied edits, so the checkpoint before the earliest of
    // them is what stands. The lowerer replays the survivors as ordinary ops after it.
    void runUndo(Run& r, const ScriptOp& op) {
      const int steps = op.nums.isEmpty() ? 1 : static_cast<int>(op.nums[0]);
      llm::EditState back;
      for (int done = 0; done < steps && !r.marks.isEmpty(); ++done) back = r.marks.takeLast();
      r.target.restoreEdit(back);
    }

    bool runOp(Run& r, const ScriptOp& op) {
      QString err;
      switch (op.kind) {
        case ScriptOpKind::OPEN:
          return runOpen(r, op);
        case ScriptOpKind::FRAME:
          return runFrame(r, op);
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
          // §7 resolves redo when it lowers, so this only ever runs if a future core emits one.
          r.target.stepHistory(true, op.nums.isEmpty() ? 1 : static_cast<int>(op.nums[0]));
          return true;
        case ScriptOpKind::SAVE: {
          const QString name = strAt(op, 0);
          if (!r.target.saveProject(name, QString(), &err)) { fail(r, err, op); return false; }
          return true;
        }
      }
      return true;
    }

  }  // namespace

  ScriptRunResult runScript(const ScriptDoc& program, llm::PlanTarget& target) {
    Run r{program, target, {}, {}};

    for (const model::ScriptDiagnostic& d : program.getDiagnostics()) {
      if (!d.isError) continue;
      r.out.isOk = false;
      r.out.error = d.message;
      r.out.line = d.line;
      r.out.col = d.col;
      return r.out;   // an erroring script runs nothing at all
    }

    const auto& ops = program.getOps();
    const bool bringsItsOwn = !ops.isEmpty() && ops.front().kind == ScriptOpKind::OPEN;
    if (!bringsItsOwn && !target.hasImage()) {
      r.out.isOk = false;
      r.out.error = QStringLiteral("open an image first");
      return r.out;
    }

    for (const ScriptOp& op : ops) {
      llm::EditState mark;
      if (isEdit(op.kind) && target.captureEdit(mark)) r.marks.push_back(mark);
      if (!runOp(r, op)) return r.out;
      ++r.out.ops;
    }
    return r.out;
  }

  ScriptRunResult runScript(const QString& text, llm::PlanTarget& target) {
    return runScript(ScriptDoc::parse(text), target);
  }

  ScriptRunResult runScriptFile(const QString& path, llm::PlanTarget& target) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      ScriptRunResult out;
      out.isOk = false;
      out.error = QStringLiteral("could not read '%1'").arg(path);
      return out;
    }
    return runScript(QString::fromUtf8(file.readAll()), target);
  }

}  // namespace stencil::gui
