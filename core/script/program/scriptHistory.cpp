#include "diagnostics.hpp"
#include "undo.hpp"
#include "values.hpp"
#include "text.hpp"

namespace stencil::core::script {

  namespace {

    // Built exactly like scriptLower's normalizedText, so a selector and the edit it
    // names produce the same string.
    std::string selectorText(const Stmt& st) {
      std::string out = toLowerAscii(st.args[0].text.substr(1));
      for (std::size_t i = 1; i < st.args.size(); ++i) {
        if (st.args[i].kind == TokenKind::COMMENT) continue;
        out.push_back(' ');
        out += toLowerAscii(st.args[i].text);
      }
      return out;
    }

  }  // namespace

  bool applyHistoryStmt(const Stmt& st, bool isRedo, EditLedger& ledger,
                        std::vector<Diagnostic>& diags) {
    std::string reason;

    if (isRedo) {
      int times = 1;
      if (!st.args.empty() && st.args[0].kind == TokenKind::NUMBER)
        times = parseIntClamped(st.args[0].text);
      if (times < 1) times = 1;
      if (!ledger.redo(times, reason)) {
        diags.push_back(makeDiag(Severity::WARNING, "W_NOTHING_TO_REDO", st, reason));
        return false;
      }
      return true;
    }

    if (st.args.empty()) {
      if (!ledger.undoIndex(0, reason)) {
        diags.push_back(makeDiag(Severity::ERROR, "E_UNDO_NO_EDITS", st, reason));
        return false;
      }
      return true;
    }

    if (st.args[0].kind == TokenKind::DIRECTIVE) {
      bool ambiguous = false;
      if (!ledger.undoByText(selectorText(st), ambiguous, reason)) {
        diags.push_back(makeDiag(Severity::ERROR, "E_UNDO_OUT_OF_RANGE", st, reason));
        return false;
      }
      if (ambiguous)
        diags.push_back(makeDiag(Severity::WARNING, "W_AMBIGUOUS_UNDO", st,
                                 "several edits match — the last one was undone"));
      return true;
    }

    bool any = false;
    for (const Token& t : st.args) {
      if (t.kind == TokenKind::PUNCT) continue;
      if (t.kind != TokenKind::NUMBER) {
        diags.push_back(makeDiag(Severity::ERROR, "E_BAD_TOKEN", t,
                                 "'" + t.text + "' is not an edit number"));
        return false;
      }
      const int selector = parseIntClamped(t.text);
      if (selector == 0) {
        diags.push_back(makeDiag(Severity::ERROR, "E_UNDO_OUT_OF_RANGE", t,
                                 "edits are numbered from 1; use -1 for the last one"));
        return false;
      }
      if (!ledger.undoIndex(selector, reason)) {
        diags.push_back(makeDiag(Severity::ERROR,
                                 ledger.editCount() == 0 ? "E_UNDO_NO_EDITS"
                                                         : "E_UNDO_OUT_OF_RANGE",
                                 t, reason));
        return false;
      }
      any = true;
    }
    if (!any && !ledger.undoIndex(0, reason)) {
      diags.push_back(makeDiag(Severity::ERROR, "E_UNDO_NO_EDITS", st, reason));
      return false;
    }
    return true;
  }

}  // namespace stencil::core::script
