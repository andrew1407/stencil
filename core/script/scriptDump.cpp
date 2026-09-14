#include "scriptDump.hpp"

#include "scriptProgram.hpp"

#include <array>
#include <cmath>
#include <string_view>

namespace stencil::core::script {

  namespace {

    constexpr std::array<std::string_view, 10> OP_NAMES = {
        "open", "frame", "crop", "filter", "line", "rect", "layout", "save", "undo", "redo"};
    constexpr std::array<std::string_view, 5> SOURCE_KIND_NAMES = {"project", "file", "url",
                                                                   "dir", "glob"};
    // Past 2^53 a double holds no more integers exactly, and the cast would be undefined.
    constexpr double INTEGRAL_LIMIT = 9007199254740992.0;

    // Trailing zeros make a dump churn on a harmless refactor; print the shortest exact form.
    std::string num(double v) {
      if (std::isfinite(v) && std::fabs(v) < INTEGRAL_LIMIT && v == std::trunc(v))
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
             std::string(SOURCE_KIND_NAMES[static_cast<std::size_t>(b.kind)]) + " " +
             quoted(b.source) + "\n";

      for (int k = 0; k < b.opCount; ++k) {
        const Op& op = ops[static_cast<std::size_t>(b.opStart + k)];
        out += "  op " + std::string(OP_NAMES[static_cast<std::size_t>(op.kind)]);
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
