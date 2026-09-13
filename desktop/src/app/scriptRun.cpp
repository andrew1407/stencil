#include "scriptRun.hpp"

#include "ScriptDoc.hpp"
#include "planExecutor.hpp"

#include <QFile>

namespace stencil::gui {

  namespace {

    using model::ScriptOp;
    using model::ScriptOpKind;
    using model::ScriptDoc;

    // Keeps `ops`: a failure part-way leaves the edits already applied, and the callers
    // refresh the window when any of them ran.
    void fail(ScriptRunResult& out, const QString& message, const ScriptOp& op) {
      out.ok = false;
      out.error = message;
      out.line = op.line;
      out.col = op.col;
    }

    QString strAt(const ScriptOp& op, int i) {
      return i < op.strs.size() ? op.strs[i] : QString();
    }

    template <class Lines>
    bool runOp(const ScriptOp& op, llm::PlanTarget& target, Lines& drawn, ScriptRunResult& out) {
      QString err;
      switch (op.kind) {
        case ScriptOpKind::OPEN: {
          const QString spec = strAt(op, 0);
          const bool url = spec.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
                        || spec.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive);
          const bool opened = url ? target.openUrl(spec, false, &err)
                                  : target.openFile(spec, &err);
          if (!opened) { fail(out, err.isEmpty() ? QStringLiteral("could not open '%1'").arg(spec) : err, op); return false; }
          return true;
        }
        case ScriptOpKind::FRAME: {
          const int index = op.nums.isEmpty() ? 0 : static_cast<int>(op.nums[0]);
          if (!target.extractFrames({index}, &err)) { fail(out, err, op); return false; }
          return true;
        }
        case ScriptOpKind::CROP: {
          bool resolved = false;
          const auto rect = ScriptDoc::cropRect(op, target.workingSize(), &resolved);
          if (!resolved) { fail(out, QStringLiteral("this crop resolves to nothing"), op); return false; }
          if (!target.applyCropRect(rect)) { fail(out, QStringLiteral("the crop was refused"), op); return false; }
          return true;
        }
        case ScriptOpKind::FILTER: {
          const QString mode = strAt(op, 0);
          target.setImageFilter(mode, mode == QStringLiteral("custom") ? strAt(op, 1) : QString());
          return true;
        }
        case ScriptOpKind::LINE:
        case ScriptOpKind::RECT: {
          bool resolved = false;
          ScriptDoc::appendLine(drawn, op, target.workingSize(), &resolved);
          if (!resolved) { fail(out, QStringLiteral("this shape resolves to nothing"), op); return false; }
          target.setLayoutLines(drawn);
          return true;
        }
        case ScriptOpKind::LAYOUT:
          fail(out, QStringLiteral("@layout needs a file the app can read — open it instead"), op);
          return false;
        case ScriptOpKind::UNDO:
        case ScriptOpKind::REDO: {
          const int steps = op.nums.isEmpty() ? 1 : static_cast<int>(op.nums[0]);
          target.stepHistory(op.kind == ScriptOpKind::REDO, steps);
          return true;
        }
        case ScriptOpKind::SAVE: {
          const QString name = strAt(op, 0);
          if (!target.saveProject(name, QString(), &err)) { fail(out, err, op); return false; }
          return true;
        }
      }
      return true;
    }

  }  // namespace

  ScriptRunResult runScript(const QString& text, llm::PlanTarget& target) {
    ScriptRunResult out;
    const ScriptDoc program = ScriptDoc::parse(text);

    for (const model::ScriptDiagnostic& d : program.diagnostics()) {
      if (!d.error) continue;
      out.ok = false;
      out.error = d.message;
      out.line = d.line;
      out.col = d.col;
      return out;   // an erroring script runs nothing at all
    }

    const auto& ops = program.ops();
    const bool bringsItsOwn = !ops.isEmpty() && ops.front().kind == ScriptOpKind::OPEN;
    if (!bringsItsOwn && !target.hasImage()) {
      out.ok = false;
      out.error = QStringLiteral("open an image first");
      return out;
    }

    auto drawn = ScriptDoc::emptyLines();
    for (const ScriptOp& op : ops) {
      if (!runOp(op, target, drawn, out)) return out;
      ++out.ops;
    }
    return out;
  }

  ScriptRunResult runScriptFile(const QString& path, llm::PlanTarget& target) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      ScriptRunResult out;
      out.ok = false;
      out.error = QStringLiteral("could not read '%1'").arg(path);
      return out;
    }
    return runScript(QString::fromUtf8(file.readAll()), target);
  }

}  // namespace stencil::gui
