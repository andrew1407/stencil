#include "scriptArgs.hpp"
#include "scriptDiagnostics.hpp"
#include "scriptValues.hpp"
#include "text.hpp"

#include <cstdlib>

namespace stencil::core::script {

  namespace {

    bool isStyleWord(const std::string& w) {
      return w == "solid" || w == "dashed" || w == "dotted";
    }

    // One comma group of `@use line`: a colour, a style word, a width, `fill <c>`,
    // `point [<c>] [<size>]`, or a bare unit. Order between groups never matters.
    bool applyGroup(const std::vector<Token>& group, EvalState& state, bool& sawColor,
                    std::vector<Diagnostic>& diags) {
      ArgCursor c{&group, 0};
      while (!c.atEnd()) {
        const Token& t = c.peek();
        const std::string low = toLowerAscii(t.text);

        if (t.kind == TokenKind::NUMBER) {
          std::string number, unit;
          readLengthRaw(c, number, unit);
          state.style.thickness = std::strtod(number.c_str(), nullptr);
          continue;
        }
        if (isStyleWord(low)) {
          state.style.style = low;
          ++c.i;
          continue;
        }
        if (isUnitWord(low)) {
          state.unit = low;
          ++c.i;
          continue;
        }
        if (low == "fill") {
          ++c.i;
          if (c.atEnd() || !isColorToken(c.peek())) {
            diags.push_back(makeDiag(Severity::ERROR, "E_BAD_TOKEN", t, "'fill' needs a colour"));
            return false;
          }
          state.style.fillColor = c.peek().text;
          ++c.i;
          continue;
        }
        if (low == "point") {
          ++c.i;
          if (!c.atEnd() && isColorToken(c.peek())) {
            state.style.pointColor = c.peek().text;
            ++c.i;
          }
          if (!c.atEnd() && c.peek().kind == TokenKind::NUMBER) {
            std::string number, unit;
            readLengthRaw(c, number, unit);
            state.style.pointSize = std::strtod(number.c_str(), nullptr);
          }
          continue;
        }
        if (isColorToken(t)) {
          if (sawColor) {
            diags.push_back(makeDiag(Severity::ERROR, "E_DUP_LINE_COLOR", t,
                                     "'@use line' already has a stroke colour"));
            return false;
          }
          state.style.color = t.text;
          sawColor = true;
          ++c.i;
          continue;
        }
        diags.push_back(makeDiag(Severity::ERROR, "E_BAD_TOKEN", t,
                                 "'" + t.text + "' is not a line-style word"));
        return false;
      }
      return true;
    }

  }  // namespace

  bool argsUse(const Stmt& st, EvalState& state, bool& isStencilUse,
               std::vector<Diagnostic>& diags) {
    isStencilUse = false;
    if (st.args.empty()) {
      diags.push_back(makeDiag(Severity::ERROR, "E_ARG_COUNT", argErrorToken(st),
                               "@use needs a unit, 'line …' or 'stencil <name>'"));
      return false;
    }

    const std::string first = toLowerAscii(st.args[0].text);
    if (first == "stencil") {
      isStencilUse = true;
      return true;
    }
    if (isUnitWord(first) && st.args.size() == 1) {
      state.unit = first;
      return true;
    }
    if (first != "line") {
      diags.push_back(makeDiag(Severity::ERROR, "E_BAD_TOKEN", st.args[0],
                               "'@use " + st.args[0].text +
                                   "' — expected a unit, 'line' or 'stencil'"));
      return false;
    }

    std::vector<Token> group;
    bool sawColor = false;
    for (std::size_t i = 1; i < st.args.size(); ++i) {
      if (isPunct(st.args[i], ",")) {
        if (!applyGroup(group, state, sawColor, diags)) return false;
        group.clear();
        continue;
      }
      group.push_back(st.args[i]);
    }
    return applyGroup(group, state, sawColor, diags);
  }

}  // namespace stencil::core::script
