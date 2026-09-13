#include "ScriptDoc.hpp"

#include "scriptProgram.hpp"  // core/script — this file is the seam that may include it

namespace stencil::model {

  namespace {

    namespace cs = stencil::core::script;

    QString qstr(const std::string& s) { return QString::fromStdString(s); }

    ScriptToken toToken(const cs::Token& t) {
      ScriptToken out;
      out.line = t.line;
      out.col = t.col;
      out.len = t.len;
      out.kind = static_cast<ScriptTokenKind>(static_cast<int>(t.kind));
      return out;
    }

    ScriptDiagnostic toDiagnostic(const cs::Diagnostic& d) {
      ScriptDiagnostic out;
      out.error = d.severity == cs::Severity::ERROR;
      out.code = qstr(d.code);
      out.line = d.line;
      out.col = d.col;
      out.len = d.len;
      out.message = qstr(d.message);
      return out;
    }

    ScriptOp toOp(const cs::Op& op) {
      ScriptOp out;
      out.kind = static_cast<ScriptOpKind>(static_cast<int>(op.kind));
      out.block = op.block;
      out.line = op.line;
      out.col = op.col;
      out.editIndex = op.editIndex;
      for (const std::string& s : op.strs) out.strs.push_back(qstr(s));
      for (const std::string& s : op.toks) out.toks.push_back(qstr(s));
      for (double n : op.nums) out.nums.push_back(n);
      return out;
    }

    // Rebuilds the core op a resolve needs, from the Qt copy the GUI carries.
    cs::Op fromOp(const ScriptOp& op) {
      cs::Op out;
      out.kind = static_cast<cs::OpKind>(static_cast<int>(op.kind));
      for (const QString& s : op.strs) out.strs.push_back(s.toStdString());
      for (const QString& s : op.toks) out.toks.push_back(s.toStdString());
      for (double n : op.nums) out.nums.push_back(n);
      return out;
    }

    // CSS pixels per cm at 96 dpi — the basis the crop parser and the browser share.
    constexpr double PX_PER_CM = 96.0 / 2.54;

  }  // namespace

  ScriptDoc ScriptDoc::parse(const QString& text) {
    const QByteArray utf8 = text.toUtf8();
    const cs::ScriptProgram p = cs::ScriptProgram::parse(utf8.constData(), utf8.size());

    ScriptDoc out;
    for (const cs::Token& t : p.tokens()) out.tokens_.push_back(toToken(t));
    for (const cs::Diagnostic& d : p.diagnostics()) out.diagnostics_.push_back(toDiagnostic(d));
    for (const cs::Op& op : p.ops()) out.ops_.push_back(toOp(op));
    for (const cs::Block& b : p.blocks()) {
      ScriptBlock block;
      block.source = qstr(b.source);
      block.kind = static_cast<ScriptSourceKind>(static_cast<int>(b.kind));
      block.frame = b.frame;
      block.opStart = b.opStart;
      block.opCount = b.opCount;
      out.blocks_.push_back(block);
    }
    return out;
  }

  bool ScriptDoc::hasErrors() const {
    for (const ScriptDiagnostic& d : diagnostics_)
      if (d.error) return true;
    return false;
  }

  QVector<double> ScriptDoc::resolve(const ScriptOp& op, QSize imageSize) {
    const cs::Op core = fromOp(op);
    double buf[2 * (cs::MAX_POINTS_PER_LINE + 1)];
    const int n = cs::resolveOp(core, static_cast<double>(imageSize.width()),
                                static_cast<double>(imageSize.height()), PX_PER_CM, PX_PER_CM,
                                buf, static_cast<int>(sizeof(buf) / sizeof(buf[0])));
    QVector<double> out;
    for (int i = 0; i < n; ++i) out.push_back(buf[i]);
    return out;
  }

  core::CropRect ScriptDoc::cropRect(const ScriptOp& op, QSize imageSize, bool* ok) {
    const QVector<double> r = resolve(op, imageSize);
    if (ok) *ok = r.size() >= 4;
    core::CropRect rect;
    if (r.size() < 4) return rect;
    rect.x = r[0];
    rect.y = r[1];
    rect.width = r[2];
    rect.height = r[3];
    return rect;
  }

  /* A shape op becomes one Line: the resolved points, then thickness and pointSize.
   * `locked` is what closes it and enables the fill. */
  void ScriptDoc::appendLine(core::Lines& lines, const ScriptOp& op, QSize imageSize, bool* ok) {
    const QVector<double> r = resolve(op, imageSize);
    if (ok) *ok = r.size() >= 6;
    if (r.size() < 6) return;

    core::Line line;
    for (int i = 0; i + 3 < r.size(); i += 2) line.points.push_back({r[i], r[i + 1]});
    auto str = [&](int i) { return i < op.strs.size() ? op.strs[i].toStdString() : std::string(); };
    line.color = str(0);
    line.style = str(1);
    line.fillColor = str(2);
    line.pointColor = str(3);
    line.thickness = r[r.size() - 2];
    line.pointSize = r[r.size() - 1];
    line.locked = op.kind == ScriptOpKind::RECT;
    lines.push_back(line);
  }

}  // namespace stencil::model
