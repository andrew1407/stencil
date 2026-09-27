#pragma once
// Private seam between the .stc runner's TUs: one run's state and the ops that apply at once
// (scriptRunOps.cpp). The run itself, and the loads that wait on I/O, are scriptRun.cpp.
#include "scriptRun.hpp"

#include "ScriptDoc.hpp"
#include "planExecutor.hpp"

#include <QVector>

namespace stencil::gui::scriptrun {

  /* One run. `marks` is a checkpoint per applied edit, newest last: an `undo` reverts to one
   * without stepping the user's own history (contracts/stc §7). A failure part-way keeps whatever
   * already ran. */
  struct RunState {
    const model::ScriptDoc program;
    llm::PlanTarget& target;
    QVector<core::EditorMemento> marks;
    ScriptRunResult out;
  };

  // The numbered edits of §7; @frame and @source start a fresh set.
  bool isEdit(model::ScriptOpKind kind);
  void fail(RunState& r, const QString& message, const model::ScriptOp& op);
  QString strAt(const model::ScriptOp& op, int i);
  // Every op but @source and @frame; false = the run stops here, and `out` says why.
  bool runOp(RunState& r, const model::ScriptOp& op);

}  // namespace stencil::gui::scriptrun
