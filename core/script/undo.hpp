#pragma once
#include "parser.hpp"
#include "types.hpp"

// Per-block edit bookkeeping for @undo / @redo. Port target: browser/js/core/script/undo.js.
namespace stencil::core::script {

  /* Edits are numbered 1..n as written and never renumbered. At each @save the ledger re-derives
   * the state: one undo{steps} back to the last agreeing edit, then a replay of the survivors. */
  class EditLedger {
   public:
    // Records an emitted edit; `text` is the normalized source `@undo @line …` matches on.
    int addEdit(Op op, std::string text);

    // Selectors: 0 = the last live edit, N = that edit, -N = N-th from the end.
    bool undoIndex(int selector, std::string& reason);

    // Undoes the last edit whose normalized text equals `text`; `ambiguous` when several did.
    bool undoByText(const std::string& text, bool& ambiguous, std::string& reason);

    bool redo(int times, std::string& reason);

    // Emits the undo{steps} + replay that makes the applied state equal the live set. False
    // when that would reach MAX_OPS: nothing is appended, so a cycle cannot multiply ops.
    bool reconcile(std::vector<Op>& out, int block, int line, int col);

    // A new @frame starts a fresh base: nothing from before is undoable.
    void reset();

    int editCount() const { return static_cast<int>(edits.size()); }

   private:
    struct EditRec {
      Op op;
      std::string text;
      bool live = true;
    };
    std::vector<EditRec> edits;
    std::vector<int> applied;
    std::vector<int> removed;  // LIFO, what @redo brings back
  };

  // Applies one `@undo`/`@redo` statement to the ledger.
  bool applyHistoryStmt(const Stmt& st, bool isRedo, EditLedger& ledger,
                        std::vector<Diagnostic>& diags);

}  // namespace stencil::core::script
