#include "ScriptProgram.hpp"

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

  ScriptProgram ScriptProgram::parse(const QString& text) {
    const QByteArray utf8 = text.toUtf8();
    const cs::ScriptProgram p = cs::ScriptProgram::parse(utf8.constData(), utf8.size());

    ScriptProgram out;
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

  bool ScriptProgram::hasErrors() const {
    for (const ScriptDiagnostic& d : diagnostics_)
      if (d.error) return true;
    return false;
  }

  QVector<double> ScriptProgram::resolve(const ScriptOp& op, QSize imageSize) {
    const cs::Op core = fromOp(op);
    double buf[2 * (cs::MAX_POINTS_PER_LINE + 1)];
    const int n = cs::resolveOp(core, static_cast<double>(imageSize.width()),
                                static_cast<double>(imageSize.height()), PX_PER_CM, PX_PER_CM,
                                buf, static_cast<int>(sizeof(buf) / sizeof(buf[0])));
    QVector<double> out;
    for (int i = 0; i < n; ++i) out.push_back(buf[i]);
    return out;
  }

}  // namespace stencil::model
