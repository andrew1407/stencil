#include "scriptUndo.hpp"

#include <algorithm>
#include <utility>

namespace stencil::core::script {

  int EditLedger::addEdit(Op op, std::string text) {
    EditRec rec;
    rec.op = std::move(op);
    rec.text = std::move(text);
    edits.push_back(std::move(rec));
    applied.push_back(static_cast<int>(edits.size()) - 1);
    return static_cast<int>(edits.size());
  }

  bool EditLedger::undoIndex(int selector, std::string& reason) {
    const int n = static_cast<int>(edits.size());
    if (n == 0) {
      reason = "there is no edit to undo in this block";
      return false;
    }
    int idx;
    if (selector == 0) {  // bare @undo — the last edit still live
      idx = -1;
      for (int i = n - 1; i >= 0; --i)
        if (edits[static_cast<std::size_t>(i)].live) { idx = i; break; }
      if (idx < 0) {
        reason = "every edit in this block is already undone";
        return false;
      }
    } else if (selector > 0) {
      idx = selector - 1;
    } else {
      idx = n + selector;  // -1 is the last edit
    }
    if (idx < 0 || idx >= n) {
      reason = "edit " + std::to_string(selector) + " is outside this block's " +
               std::to_string(n) + " edit(s)";
      return false;
    }
    EditRec& rec = edits[static_cast<std::size_t>(idx)];
    if (!rec.live) {
      reason = "edit " + std::to_string(idx + 1) + " is already undone";
      return false;
    }
    rec.live = false;
    removed.push_back(idx);
    return true;
  }

  bool EditLedger::undoByText(const std::string& text, bool& ambiguous, std::string& reason) {
    ambiguous = false;
    int found = -1, matches = 0;
    for (int i = static_cast<int>(edits.size()) - 1; i >= 0; --i) {
      if (edits[static_cast<std::size_t>(i)].text != text) continue;
      ++matches;
      if (found < 0 && edits[static_cast<std::size_t>(i)].live) found = i;
    }
    if (found < 0) {
      reason = matches > 0 ? "that edit is already undone"
                           : "no edit in this block matches '" + text + "'";
      return false;
    }
    ambiguous = matches > 1;
    edits[static_cast<std::size_t>(found)].live = false;
    removed.push_back(found);
    return true;
  }

  bool EditLedger::redo(int times, std::string& reason) {
    if (removed.empty()) {
      reason = "nothing to redo in this block";
      return false;
    }
    for (int k = 0; k < times && !removed.empty(); ++k) {
      edits[static_cast<std::size_t>(removed.back())].live = true;
      removed.pop_back();
    }
    return true;
  }

  bool EditLedger::reconcile(std::vector<Op>& out, int block, int line, int col) {
    std::vector<int> live;
    for (std::size_t i = 0; i < edits.size(); ++i)
      if (edits[i].live) live.push_back(static_cast<int>(i));

    std::size_t k = 0;
    while (k < applied.size() && k < live.size() && applied[k] == live[k]) ++k;

    const std::size_t steps = applied.size() - k;
    const std::size_t appended = (steps > 0 ? 1u : 0u) + (live.size() - k);
    if (appended > 0 && out.size() + appended >= static_cast<std::size_t>(MAX_OPS))
      return false;

    if (steps > 0) {
      Op undo;
      undo.kind = OpKind::UNDO;
      undo.block = block;
      undo.line = line;
      undo.col = col;
      undo.nums.assign(1, static_cast<double>(steps));
      out.push_back(std::move(undo));
    }
    for (std::size_t i = k; i < live.size(); ++i)
      out.push_back(edits[static_cast<std::size_t>(live[i])].op);
    applied = std::move(live);
    return true;
  }

  void EditLedger::reset() {
    edits.clear();
    applied.clear();
    removed.clear();
  }

}  // namespace stencil::core::script
