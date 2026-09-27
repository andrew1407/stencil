#include "dump.hpp"

#include "jsNumber.hpp"
#include "scriptProgram.hpp"

#include <array>
#include <string_view>

namespace stencil::core::script {

  namespace {

    constexpr std::array<std::string_view, 10> OP_NAMES = {
        "open", "frame", "crop", "filter", "line", "rect", "layout", "save", "undo", "redo"};
    constexpr std::array<std::string_view, 5> SOURCE_KIND_NAMES = {"project", "file", "url",
                                                                   "dir", "glob"};
    // JavaScript's String(v), from the one formatter core has (to_chars would add ~150 KB of
    // Ryu tables to wasm), so dump.js prints the same line.
    std::string num(double v) { return json::jsNumberToString(v); }

    std::string quoted(const std::string& s) { return "\"" + s + "\""; }

  }  // namespace

  std::string dumpProgram(const ScriptProgram& program) {
    std::string out;
    const std::vector<Op>& ops = program.getOps();

    for (std::size_t bi = 0; bi < program.getBlocks().size(); ++bi) {
      const Block& b = program.getBlocks()[bi];
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
    for (const Diagnostic& d : program.getDiagnostics())
      out += num(d.line) + ":" + num(d.col) + ":" + num(d.len) + ": " +
             (d.severity == Severity::ERROR ? "error: " : "warning: ") + d.message + " [" +
             d.code + "]\n";
    return out;
  }

}  // namespace stencil::core::script
