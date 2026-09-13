#pragma once

#include <QString>

namespace stencil::llm { class PlanTarget; }

// Running a .stc against the editor. The core lowers the language to an op stream; this maps
// each op onto the SAME PlanTarget the assistant's op plans drive, so a scripted edit and a
// clicked one take one path. The language is normative in stc-contract/stc-contract.md.
namespace stencil::gui {

  struct ScriptRunResult {
    bool ok = true;
    int ops = 0;        // how many ops actually ran
    QString error;      // empty when ok
    int line = 0;       // where it stopped, 1-based
    int col = 0;
  };

  /* Runs `text`. A script with any error runs NOTHING and comes back with the first one; a
   * failure part-way leaves the edits already applied and names the line that stopped it. */
  ScriptRunResult runScript(const QString& text, llm::PlanTarget& target);

  // Reads a .stc from disk and runs it. A read failure is reported like a script error.
  ScriptRunResult runScriptFile(const QString& path, llm::PlanTarget& target);

}  // namespace stencil::gui
