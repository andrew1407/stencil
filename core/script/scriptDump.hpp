#pragma once
#include <string>

// The canonical text form every fixture walker compares against.
// Port target: browser/js/core/scriptDump.js.
namespace stencil::core::script {

  class ScriptProgram;

  // One line per block and per op, in order. Stable across surfaces — it IS the fixture
  // format, so a change here re-records every `<name>.dump.txt`.
  std::string dumpProgram(const ScriptProgram& program);

  // "line:col:len: error: message [CODE]" per diagnostic, in emission order.
  std::string dumpDiagnostics(const ScriptProgram& program);

}  // namespace stencil::core::script
