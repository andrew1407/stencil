#include "scriptLower.hpp"

#include "scriptArgs.hpp"
#include "scriptDiagnostics.hpp"
#include "scriptTemplates.hpp"
#include "scriptUndo.hpp"
#include "scriptValues.hpp"
#include "text.hpp"

#include <cstdlib>

namespace stencil::core::script {

  namespace {

    Token tokenOf(const Stmt& s) {
      Token t;
      t.line = s.line;
      t.col = s.col;
      t.len = s.len;
      t.text = "@" + s.directive;
      return t;
    }

    // What `@undo @line …` matches on: the directive plus its argument words.
    std::string normalizedText(const Stmt& s) {
      std::string out = s.directive;
      for (const Token& t : s.args) {
        if (t.kind == TokenKind::COMMENT) continue;
        out.push_back(' ');
        out += toLowerAscii(t.text);
      }
      return out;
    }

    bool isEditDirective(const std::string& d) {
      return d == "crop" || d == "filter" || d == "line" || d == "rect" || d == "layout";
    }

    Op blankOp(OpKind kind, const Stmt& st, int block) {
      Op op;
      op.kind = kind;
      op.block = block;
      op.line = st.line;
      op.col = st.col;
      op.len = st.len;
      return op;
    }

  }  // namespace

  LowerResult lowerScript(ParseResult& parsed) {
    LowerResult out;
    out.diagnostics = parsed.diagnostics;
    std::vector<Stmt> emptyBlocks;

    for (std::size_t bi = 0; bi < parsed.blocks.size(); ++bi) {
      RawBlock& raw = parsed.blocks[bi];
      const int blockIndex = static_cast<int>(out.blocks.size());

      Block block;
      block.line = raw.implicit ? 1 : raw.header.line;
      block.opStart = static_cast<int>(out.ops.size());
      if (!raw.implicit) {
        block.source = joinWords(raw.header.args);
        block.kind = classifySource(block.source);
        if (static_cast<int>(block.source.size()) > MAX_SOURCE_CHARS) {
          out.diagnostics.push_back(makeDiag(Severity::ERROR, "E_LIMIT_SOURCE",
                                             tokenOf(raw.header), "the @source spec is too long"));
          block.source.resize(MAX_SOURCE_CHARS);
        }
        Op open = blankOp(OpKind::OPEN, raw.header, blockIndex);
        open.strs.assign(1, block.source);
        open.nums.assign(1, static_cast<double>(block.kind));
        out.ops.push_back(open);
      }

      // Expand templates first, so edit numbering counts what a template contributed.
      std::vector<Stmt> body;
      for (const Stmt& st : raw.body) {
        if (st.directive != "use") { body.push_back(st); continue; }
        bool isStencilUse = false;
        if (!st.args.empty() && toLowerAscii(unquoteWord(st.args[0].text)) == "stencil")
          isStencilUse = true;
        if (!isStencilUse) { body.push_back(st); continue; }
        expandStencilUse(st, parsed.templates, 1, body, out.diagnostics);
      }

      EvalState state;
      EditLedger ledger;
      bool sawSave = false, sawEdit = false;
      std::vector<int> frames;

      for (const Stmt& st : body) {
        const std::string& d = st.directive;

        if (d == "use") {
          bool isStencilUse = false;
          argsUse(st, state, isStencilUse, out.diagnostics);
          continue;
        }

        if (d == "frame") {
          if (raw.implicit) {
            out.diagnostics.push_back(makeDiag(Severity::ERROR, "E_FRAME_OUTSIDE_SOURCE",
                                               tokenOf(st),
                                               "@frame needs a @source block naming a video"));
            continue;
          }
          Op op = blankOp(OpKind::FRAME, st, blockIndex);
          if (!argsFrame(st, op, out.diagnostics)) continue;
          const int idx = static_cast<int>(op.nums[0]);
          bool dup = false;
          for (int f : frames)
            if (f == idx) { dup = true; break; }
          if (dup) {
            out.diagnostics.push_back(makeDiag(Severity::ERROR, "E_DUPLICATE_FRAME", tokenOf(st),
                                               "frame " + std::to_string(idx) +
                                                   " is already used in this block"));
            continue;
          }
          frames.push_back(idx);
          ledger.reconcile(out.ops, blockIndex, st.line, st.col);
          ledger.reset();
          out.ops.push_back(op);
          continue;
        }

        if (d == "undo" || d == "redo") {
          if (!applyHistoryStmt(st, d == "redo", ledger, out.diagnostics)) continue;
          continue;
        }

        if (d == "save") {
          Op op = blankOp(OpKind::SAVE, st, blockIndex);
          if (!argsSave(st, op, out.diagnostics)) continue;
          ledger.reconcile(out.ops, blockIndex, st.line, st.col);
          out.ops.push_back(op);
          sawSave = true;
          continue;
        }

        if (!isEditDirective(d)) {
          out.diagnostics.push_back(makeDiag(Severity::ERROR, "E_BAD_TOKEN", tokenOf(st),
                                             "'@" + d + "' cannot be used here"));
          continue;
        }

        Op op;
        bool ok = false;
        if (d == "crop") {
          op = blankOp(OpKind::CROP, st, blockIndex);
          ok = argsCrop(st, state, op, out.diagnostics);
        } else if (d == "filter") {
          op = blankOp(OpKind::FILTER, st, blockIndex);
          ok = argsFilter(st, op, out.diagnostics);
        } else if (d == "layout") {
          op = blankOp(OpKind::LAYOUT, st, blockIndex);
          ok = argsLayout(st, op, out.diagnostics);
        } else {
          const bool locked = d == "rect";
          op = blankOp(locked ? OpKind::RECT : OpKind::LINE, st, blockIndex);
          ok = argsShape(st, state, locked, op, out.diagnostics);
        }
        if (!ok) continue;
        if (static_cast<int>(out.ops.size()) >= MAX_OPS) {
          out.diagnostics.push_back(
              makeDiag(Severity::ERROR, "E_LIMIT_OPS", tokenOf(st), "the script has too many ops"));
          break;
        }
        op.editIndex = ledger.addEdit(op, normalizedText(st));
        out.ops.push_back(op);
        sawEdit = true;
      }

      ledger.reconcile(out.ops, blockIndex, block.line, 1);

      if (!raw.implicit && !sawEdit && !sawSave) emptyBlocks.push_back(raw.header);

      block.opCount = static_cast<int>(out.ops.size()) - block.opStart;
      out.blocks.push_back(block);
    }

    reportUnusedTemplates(parsed.templates, out.diagnostics);
    // A script with an error never runs, so "does nothing" would only add noise.
    if (!hasErrors(out.diagnostics))
      for (const Stmt& header : emptyBlocks)
        out.diagnostics.push_back(makeDiag(Severity::WARNING, "W_EMPTY_BLOCK", tokenOf(header),
                                           "this @source block does nothing"));
    return out;
  }

}  // namespace stencil::core::script
