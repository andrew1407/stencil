#include "lower.hpp"

#include "args.hpp"
#include "diagnostics.hpp"
#include "templates.hpp"
#include "undo.hpp"
#include "values.hpp"
#include "text.hpp"

#include <utility>

namespace stencil::core::script {

  namespace {

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

    Op blankOp(OpKind kind, const Stmt& st, int block) {
      Op op;
      op.kind = kind;
      op.block = block;
      op.line = st.line;
      op.col = st.col;
      op.len = st.len;
      return op;
    }

    bool isStencilUse(const Stmt& st) {
      return st.kind == Directive::USE && !st.args.empty() &&
             toLowerAscii(unquoteWord(st.args[0].text)) == "stencil";
    }

  }  // namespace

  LowerResult lowerScript(ParseResult& parsed) {
    LowerResult out;
    out.diagnostics = std::move(parsed.diagnostics);
    std::vector<Token> emptyBlocks;
    bool capped = false;  // a replay that would pass MAX_OPS stops the whole script
    int expansions = 0;   // the whole script's template fan-out, blocks included

    for (RawBlock& raw : parsed.blocks) {
      const int blockIndex = static_cast<int>(out.blocks.size());

      Block block;
      block.line = raw.implicit ? 1 : raw.header.line;
      block.opStart = static_cast<int>(out.ops.size());
      if (!raw.implicit) {
        block.source = joinWords(raw.header.args);
        block.kind = classifySource(block.source);
        if (static_cast<int>(block.source.size()) > MAX_SOURCE_CHARS) {
          out.diagnostics.push_back(makeDiag(Severity::ERROR, "E_LIMIT_SOURCE", raw.header,
                                             "the @source spec is too long"));
          block.source.resize(MAX_SOURCE_CHARS);
        }
        Op open = blankOp(OpKind::OPEN, raw.header, blockIndex);
        open.strs.assign(1, block.source);
        open.nums.assign(1, static_cast<double>(block.kind));
        out.ops.push_back(std::move(open));
      }

      // Expand templates first, so edit numbering counts what a template contributed.
      std::vector<Stmt> body;
      for (Stmt& st : raw.body) {
        if (!isStencilUse(st)) { body.push_back(std::move(st)); continue; }
        expandStencilUse(st, parsed.templates, 1, expansions, body, out.diagnostics);
        if (expansions > MAX_TEMPLATE_EXPANSIONS) { capped = true; break; }
      }
      if (capped) break;  // the capped block is not recorded, so nothing of it dumps

      EvalState state;
      EditLedger ledger;
      bool sawSave = false, sawEdit = false;
      std::vector<int> frames;

      auto rewindAndReplay = [&](int line, int col) {
        if (ledger.reconcile(out.ops, blockIndex, line, col)) return true;
        out.diagnostics.push_back(makeDiag(Severity::ERROR, "E_LIMIT_OPS", raw.header,
                                           "the script has too many ops"));
        capped = true;
        return false;
      };

      for (Stmt& st : body) {
        if (st.kind == Directive::USE) {
          bool sawStencil = false;
          argsUse(st, state, sawStencil, out.diagnostics);
          continue;
        }

        if (st.kind == Directive::FRAME) {
          if (raw.implicit) {
            out.diagnostics.push_back(makeDiag(Severity::ERROR, "E_FRAME_OUTSIDE_SOURCE", st,
                                               "@frame needs a @source block naming a video"));
            continue;
          }
          Op op = blankOp(OpKind::FRAME, st, blockIndex);
          if (!argsFrame(st, op, out.diagnostics)) continue;
          const int idx = static_cast<int>(op.nums[0]);
          bool isDuplicate = false;
          for (int f : frames)
            if (f == idx) { isDuplicate = true; break; }
          if (isDuplicate) {
            out.diagnostics.push_back(makeDiag(Severity::ERROR, "E_DUPLICATE_FRAME", st,
                                               "frame " + std::to_string(idx) +
                                                   " is already used in this block"));
            continue;
          }
          frames.push_back(idx);
          if (!rewindAndReplay(st.line, st.col)) break;
          ledger.reset();
          out.ops.push_back(std::move(op));
          continue;
        }

        if (st.kind == Directive::UNDO || st.kind == Directive::REDO) {
          applyHistoryStmt(st, st.kind == Directive::REDO, ledger, out.diagnostics);
          continue;
        }

        if (st.kind == Directive::SAVE) {
          Op op = blankOp(OpKind::SAVE, st, blockIndex);
          if (!argsSave(st, op, out.diagnostics)) continue;
          if (!rewindAndReplay(st.line, st.col)) break;
          out.ops.push_back(std::move(op));
          sawSave = true;
          continue;
        }

        if (!isEditDirective(st.kind)) {
          out.diagnostics.push_back(makeDiag(Severity::ERROR, "E_BAD_TOKEN", st,
                                             "'@" + st.directive + "' cannot be used here"));
          continue;
        }

        Op op;
        bool ok = false;
        switch (st.kind) {
          case Directive::CROP:
            op = blankOp(OpKind::CROP, st, blockIndex);
            ok = argsCrop(st, state, op, out.diagnostics);
            break;
          case Directive::FILTER:
            op = blankOp(OpKind::FILTER, st, blockIndex);
            ok = argsFilter(st, op, out.diagnostics);
            break;
          case Directive::LAYOUT:
            op = blankOp(OpKind::LAYOUT, st, blockIndex);
            ok = argsLayout(st, op, out.diagnostics);
            break;
          default: {  // @line, and @rect as a line with locked corners
            const bool locked = st.kind == Directive::RECT;
            op = blankOp(locked ? OpKind::RECT : OpKind::LINE, st, blockIndex);
            ok = argsShape(st, state, locked, op, out.diagnostics);
            break;
          }
        }
        if (!ok) continue;
        if (static_cast<int>(out.ops.size()) >= MAX_OPS) {
          out.diagnostics.push_back(
              makeDiag(Severity::ERROR, "E_LIMIT_OPS", st, "the script has too many ops"));
          break;
        }
        // Numbered before the ledger copies it, so a replayed edit dumps as the same edit.
        op.editIndex = ledger.editCount() + 1;
        ledger.addEdit(op, normalizedText(st));
        out.ops.push_back(std::move(op));
        sawEdit = true;
      }

      if (!capped) rewindAndReplay(block.line, 1);
      if (capped) break;  // the capped block is not recorded, so nothing of it dumps

      if (!raw.implicit && !sawEdit && !sawSave) emptyBlocks.push_back(tokenOfStmt(raw.header));

      block.opCount = static_cast<int>(out.ops.size()) - block.opStart;
      out.blocks.push_back(std::move(block));
    }

    reportUnusedTemplates(parsed.templates, out.diagnostics);
    // A script with an error never runs, so "does nothing" would only add noise.
    if (!hasErrors(out.diagnostics))
      for (const Token& header : emptyBlocks)
        out.diagnostics.push_back(makeDiag(Severity::WARNING, "W_EMPTY_BLOCK", header,
                                           "this @source block does nothing"));
    return out;
  }

}  // namespace stencil::core::script
