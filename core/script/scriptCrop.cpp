#include "scriptArgs.hpp"

#include "scriptDiagnostics.hpp"
#include "scriptValues.hpp"
#include "text.hpp"

namespace stencil::core::script {

  namespace {

    const char* kCropKeys[] = {"x1", "x2", "y1", "y2", "aspect"};

    bool isCropKey(const std::string& w, int& slot) {
      const std::string k = toLowerAscii(w);
      for (int i = 0; i < 5; ++i)
        if (k == kCropKeys[i]) { slot = i; return true; }
      return false;
    }

    // "-10%" flips to the far edge; an inset is the same distance from either side.
    std::string mirror(const std::string& tok) {
      if (!tok.empty() && tok[0] == '-') return tok.substr(1);
      return "-" + tok;
    }

  }  // namespace

  bool argsCrop(const Stmt& st, const EvalState& state, Op& op, std::vector<Diagnostic>& diags) {
    ArgCursor c{&st.args, 0};
    Token where = st.args.empty() ? Token{st.line, st.col, st.len, TokenKind::DIRECTIVE, "@crop"}
                                  : st.args[0];
    std::string edges[4];  // x1, x2, y1, y2
    std::string aspect;
    bool sawKey = false, sawPositional = false;
    std::vector<std::string> positional;

    while (!c.atEnd()) {
      skipPunct(c, ",");
      if (c.atEnd()) break;
      const Token& t = c.peek();
      int slot = 0;
      if (t.kind == TokenKind::IDENT && isCropKey(t.text, slot)) {
        sawKey = true;
        ++c.i;
        skipPunct(c, "=");
        if (slot == 4) {  // aspect takes a raw "W:H" word, not a length
          if (c.atEnd()) {
            diags.push_back(makeDiag(Severity::ERROR, "E_BAD_TOKEN", t, "'aspect' needs a W:H ratio"));
            return false;
          }
          aspect = unquoteWord(c.peek().text);
          ++c.i;
          // The lexer splits "3:2" on the ':'; re-join the far side.
          if (!c.atEnd() && isPunct(c.peek(), ":")) {
            ++c.i;
            if (!c.atEnd()) { aspect += ":" + c.peek().text; ++c.i; }
          }
          continue;
        }
        std::string tok;
        if (!readLength(c, state.unit, tok)) {
          diags.push_back(makeDiag(Severity::ERROR, "E_BAD_TOKEN",
                                   c.atEnd() ? t : c.peek(),
                                   "'" + toLowerAscii(t.text) + "' needs a length"));
          return false;
        }
        edges[slot] = tok;
        continue;
      }
      if (t.kind == TokenKind::NUMBER) {
        sawPositional = true;
        std::string tok;
        readLength(c, state.unit, tok);
        positional.push_back(tok);
        continue;
      }
      if (t.kind == TokenKind::IDENT && toLowerAscii(t.text) == "album") {
        op.nums.assign(1, 1.0);
        ++c.i;
        continue;
      }
      diags.push_back(makeDiag(Severity::ERROR, "E_CROP_UNKNOWN_KEY", t,
                               "'" + t.text + "' is not a crop key (x1, x2, y1, y2, aspect)"));
      return false;
    }

    if (sawKey && sawPositional) {
      diags.push_back(makeDiag(Severity::ERROR, "E_CROP_MIXED_FORM", where,
                               "@crop takes either key=value pairs or bare insets, not both"));
      return false;
    }
    if (sawPositional) {
      if (positional.size() == 1) {
        edges[0] = positional[0];
        edges[1] = mirror(positional[0]);
        edges[2] = positional[0];
        edges[3] = mirror(positional[0]);
      } else if (positional.size() == 2) {
        edges[0] = positional[0];
        edges[1] = mirror(positional[0]);
        edges[2] = positional[1];
        edges[3] = mirror(positional[1]);
      } else if (positional.size() == 4) {
        edges[0] = positional[0];
        edges[2] = positional[1];
        edges[1] = positional[2];
        edges[3] = positional[3];
      } else {
        diags.push_back(makeDiag(Severity::ERROR, "E_CROP_ARITY", where,
                                 "@crop takes 1, 2 or 4 insets, got " +
                                     std::to_string(positional.size())));
        return false;
      }
    }
    if (!sawKey && !sawPositional) {
      diags.push_back(makeDiag(Severity::ERROR, "E_ARG_COUNT", where, "@crop needs an argument"));
      return false;
    }

    op.strs.assign(1, aspect);
    op.toks.assign(edges, edges + 4);
    if (op.nums.empty()) op.nums.assign(1, 0.0);
    return true;
  }

}  // namespace stencil::core::script
