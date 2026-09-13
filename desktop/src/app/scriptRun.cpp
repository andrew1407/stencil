#include "scriptRun.hpp"

#include "ScriptDoc.hpp"
#include "cropGeometry.hpp"
#include "models.hpp"
#include "planExecutor.hpp"

#include <QFile>

namespace stencil::gui {

  namespace {

    using model::ScriptOp;
    using model::ScriptOpKind;
    using model::ScriptDoc;

    ScriptRunResult failure(const QString& message, const ScriptOp& op) {
      ScriptRunResult r;
      r.ok = false;
      r.error = message;
      r.line = op.line;
      r.col = op.col;
      return r;
    }

    QString strAt(const ScriptOp& op, int i) {
      return i < op.strs.size() ? op.strs[i] : QString();
    }

    /* A shape op becomes one Line in the project's own vocabulary: the resolved points, then
     * thickness and pointSize. `locked` is what closes it and enables the fill. */
    core::Line lineFrom(const ScriptOp& op, const QVector<double>& r) {
      core::Line line;
      for (int i = 0; i + 3 < r.size(); i += 2) line.points.push_back({r[i], r[i + 1]});
      line.color = strAt(op, 0).toStdString();
      line.style = strAt(op, 1).toStdString();
      line.fillColor = strAt(op, 2).toStdString();
      line.pointColor = strAt(op, 3).toStdString();
      line.thickness = r.size() >= 2 ? r[r.size() - 2] : 2.0;
      line.pointSize = r.isEmpty() ? 4.0 : r[r.size() - 1];
      line.locked = op.kind == ScriptOpKind::RECT;
      return line;
    }

    bool runOp(const ScriptOp& op, llm::PlanTarget& target, core::Lines& drawn,
               ScriptRunResult& out) {
      QString err;
      switch (op.kind) {
        case ScriptOpKind::OPEN: {
          const QString spec = strAt(op, 0);
          const bool url = spec.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
                        || spec.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive);
          const bool opened = url ? target.openUrl(spec, false, &err)
                                  : target.openFile(spec, &err);
          if (!opened) { out = failure(err.isEmpty() ? QStringLiteral("could not open '%1'").arg(spec) : err, op); return false; }
          return true;
        }
        case ScriptOpKind::FRAME: {
          const int index = op.nums.isEmpty() ? 0 : static_cast<int>(op.nums[0]);
          if (!target.extractFrames({index}, &err)) { out = failure(err, op); return false; }
          return true;
        }
        case ScriptOpKind::CROP: {
          const QVector<double> r = ScriptDoc::resolve(op, target.workingSize());
          if (r.size() < 4) { out = failure(QStringLiteral("this crop resolves to nothing"), op); return false; }
          core::CropRect rect;
          rect.x = r[0];
          rect.y = r[1];
          rect.width = r[2];
          rect.height = r[3];
          if (!target.applyCropRect(rect)) { out = failure(QStringLiteral("the crop was refused"), op); return false; }
          return true;
        }
        case ScriptOpKind::FILTER: {
          const QString mode = strAt(op, 0);
          target.setImageFilter(mode, mode == QStringLiteral("custom") ? strAt(op, 1) : QString());
          return true;
        }
        case ScriptOpKind::LINE:
        case ScriptOpKind::RECT: {
          const QVector<double> r = ScriptDoc::resolve(op, target.workingSize());
          if (r.size() < 6) { out = failure(QStringLiteral("this shape resolves to nothing"), op); return false; }
          drawn.push_back(lineFrom(op, r));
          target.setLayoutLines(drawn);
          return true;
        }
        case ScriptOpKind::LAYOUT:
          out = failure(QStringLiteral("@layout needs a file the app can read — open it instead"), op);
          return false;
        case ScriptOpKind::UNDO:
        case ScriptOpKind::REDO: {
          const int steps = op.nums.isEmpty() ? 1 : static_cast<int>(op.nums[0]);
          target.stepHistory(op.kind == ScriptOpKind::REDO, steps);
          return true;
        }
        case ScriptOpKind::SAVE: {
          const QString name = strAt(op, 0);
          if (!target.saveProject(name, QString(), &err)) { out = failure(err, op); return false; }
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

    if (!target.hasImage()) {
      out.ok = false;
      out.error = QStringLiteral("open an image first");
      return out;
    }

    core::Lines drawn;
    for (const ScriptOp& op : program.ops()) {
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
