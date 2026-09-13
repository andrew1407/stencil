#include "scriptDump.hpp"

#include "scriptProgram.hpp"

#include <cmath>

namespace stencil::core::script {

  namespace {

    const char* kOpNames[] = {"open",   "frame", "crop", "filter", "line",
                              "rect",   "layout", "save", "undo",  "redo"};
    const char* kSourceKinds[] = {"project", "file", "url", "dir", "glob"};

    // Trailing zeros make a dump churn on a harmless refactor; print the shortest exact form.
    std::string num(double v) {
      if (std::isfinite(v) && v == static_cast<double>(static_cast<long long>(v)))
        return std::to_string(static_cast<long long>(v));
      std::string s = std::to_string(v);
      while (s.size() > 1 && s.back() == '0') s.pop_back();
      if (!s.empty() && s.back() == '.') s.pop_back();
      return s;
    }

    std::string quoted(const std::string& s) { return "\"" + s + "\""; }

  }  // namespace

  std::string dumpProgram(const ScriptProgram& program) {
    std::string out;
    const std::vector<Op>& ops = program.ops();

    for (std::size_t bi = 0; bi < program.blocks().size(); ++bi) {
      const Block& b = program.blocks()[bi];
      out += "block " + num(static_cast<double>(bi)) + " " +
             kSourceKinds[static_cast<int>(b.kind)] + " " + quoted(b.source) + "\n";

      for (int k = 0; k < b.opCount; ++k) {
        const Op& op = ops[static_cast<std::size_t>(b.opStart + k)];
        out += "  op " + std::string(kOpNames[static_cast<int>(op.kind)]);
        if (op.editIndex > 0) out += " edit=" + num(op.editIndex);
        for (const std::string& s : op.strs) out += " " + quoted(s);
        if (!op.toks.empty()) {
          out += " [";
          for (std::size_t i = 0; i < op.toks.size(); ++i) {
            if (i) out += " ";
            out += op.toks[i].empty() ? "_" : op.toks[i];  // '_' is an edge left unset
          }
          out += "]";
        }
        for (double n : op.nums) out += " " + num(n);
        out += "\n";
      }
    }
    return out;
  }

  std::string dumpDiagnostics(const ScriptProgram& program) {
    std::string out;
    for (const Diagnostic& d : program.diagnostics())
      out += num(d.line) + ":" + num(d.col) + ":" + num(d.len) + ": " +
             (d.severity == Severity::ERROR ? "error: " : "warning: ") + d.message + " [" +
             d.code + "]\n";
    return out;
  }

}  // namespace stencil::core::script
