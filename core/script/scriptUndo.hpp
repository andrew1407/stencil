#pragma once
#include "scriptParser.hpp"
#include "scriptTypes.hpp"

// Per-block edit bookkeeping for @undo / @redo. Port target: browser/js/core/scriptUndo.js.
namespace stencil::core::script {

  /* Edits are numbered 1..n as written and never renumbered, so `@undo 2` always means
   * the second edit of the block. Undoing marks an edit dead; the ledger then RE-DERIVES
   * the image state at each @save by emitting one undo{steps} back to the last agreeing
   * edit and replaying the survivors. No adapter ever computes an undo count. */
  class EditLedger {
   public:
    // Records an edit that the caller has just emitted. `text` is its normalized source,
    // which `@undo @line …` matches against. Returns the 1-based edit index.
    int addEdit(const Op& op, const std::string& text);

    // Selector forms: 0 = the last live edit, N = that edit, -N = N-th from the end.
    // Returns false (with `reason` set) when the selector names no edit.
    bool undoIndex(int selector, std::string& reason);

    // Undoes the last edit whose normalized text equals `text`. `ambiguous` is set when
    // several matched, so the caller can warn.
    bool undoByText(const std::string& text, bool& ambiguous, std::string& reason);

    bool redo(int times, std::string& reason);

    // Emits the undo{steps} + replay that makes the applied state equal the live set.
    void reconcile(std::vector<Op>& out, int block, int line, int col);

    // A new @frame starts a fresh base: nothing from before is undoable.
    void reset();

    int editCount() const { return static_cast<int>(edits_.size()); }

   private:
    struct EditRec {
      Op op;
      std::string text;
      bool live = true;
    };
    std::vector<EditRec> edits_;
    std::vector<int> applied_;
    std::vector<int> removed_;  // LIFO, what @redo brings back
  };

  /* Applies one `@undo`/`@redo` statement to the ledger. Selectors: none (the last live
   * edit), N, -N, several at once, or `@undo @line …` matching an edit by its text. */
  bool applyHistoryStmt(const Stmt& st, bool isRedo, EditLedger& ledger,
                        std::vector<Diagnostic>& diags);

}  // namespace stencil::core::script
