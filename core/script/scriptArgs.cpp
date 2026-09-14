#include "scriptArgs.hpp"

#include "scriptDiagnostics.hpp"
#include "scriptValues.hpp"
#include "text.hpp"

namespace stencil::core::script {

  SourceKind classifySource(const std::string& spec) {
    if (spec.empty()) return SourceKind::PROJECT;
    const std::string low = toLowerAscii(spec);
    if (low.rfind("http://", 0) == 0 || low.rfind("https://", 0) == 0) return SourceKind::URL;
    if (spec.find('*') != std::string::npos || spec.find('?') != std::string::npos ||
        spec.find('[') != std::string::npos)
      return SourceKind::GLOB;
    if (spec.back() == '/') return SourceKind::DIR;
    return SourceKind::FILE;
  }

  bool argsFilter(const Stmt& st, Op& op, std::vector<Diagnostic>& diags) {
    const std::string word = joinWords(st.args);
    if (word.empty()) {
      diags.push_back(makeDiag(Severity::ERROR, "E_ARG_COUNT", argErrorToken(st),
                               "@filter needs a mode (bw, sepia, invert, contour, none) or a colour"));
      return false;
    }
    const std::string low = toLowerAscii(word);
    if (low == "bw" || low == "sepia" || low == "invert" || low == "contour" || low == "none") {
      op.strs = {low, ""};
      return true;
    }
    if (isColorToken(st.args[0]) && st.args.size() == 1) {
      op.strs = {"custom", word};
      return true;
    }
    diags.push_back(makeDiag(Severity::ERROR, "E_UNKNOWN_FILTER", argErrorToken(st),
                             "'" + word + "' is not a filter mode or a colour"));
    return false;
  }

  bool argsShape(const Stmt& st, const EvalState& state, bool locked, Op& op,
                 std::vector<Diagnostic>& diags) {
    ArgCursor c{&st.args, 0};
    std::vector<std::string> pts;
    if (!readPointList(c, state.unit, pts, diags)) return false;
    const std::size_t count = pts.size() / 2;
    if (count < 2) {
      diags.push_back(makeDiag(Severity::ERROR, "E_LINE_NEEDS_POINTS", argErrorToken(st),
                               std::string(locked ? "@rect" : "@line") +
                                   " needs at least two points"));
      return false;
    }
    op.strs = {state.style.color, state.style.style, state.style.fillColor,
               state.style.pointColor};
    op.toks = std::move(pts);
    op.nums = {state.style.thickness, state.style.pointSize, locked ? 1.0 : 0.0};
    return true;
  }

  bool argsLayout(const Stmt& st, Op& op, std::vector<Diagnostic>& diags) {
    std::vector<Token> words;
    std::string mode = "combine";
    for (const Token& t : st.args) {
      if (t.kind == TokenKind::PUNCT) continue;
      const std::string low = toLowerAscii(t.text);
      if (low == "combine" || low == "replace") { mode = low; continue; }
      words.push_back(t);
    }
    const std::string src = joinWords(words);
    if (src.empty()) {
      diags.push_back(makeDiag(Severity::ERROR, "E_ARG_COUNT", argErrorToken(st),
                               "@layout needs a path or URL"));
      return false;
    }
    op.strs = {src, mode};
    op.nums = {static_cast<double>(classifySource(src))};
    return true;
  }

  bool argsSave(const Stmt& st, Op& op, std::vector<Diagnostic>& diags) {
    (void)diags;
    op.strs.assign(1, joinWords(st.args));
    return true;
  }

  bool argsFrame(const Stmt& st, Op& op, std::vector<Diagnostic>& diags) {
    if (st.args.empty() || st.args[0].kind != TokenKind::NUMBER) {
      diags.push_back(makeDiag(Severity::ERROR, "E_ARG_COUNT", argErrorToken(st),
                               "@frame needs a frame index"));
      return false;
    }
    const int n = parseIntClamped(st.args[0].text);
    if (n < 0) {
      diags.push_back(makeDiag(Severity::ERROR, "E_BAD_TOKEN", st.args[0],
                               "a frame index cannot be negative"));
      return false;
    }
    op.nums.assign(1, static_cast<double>(n));
    return true;
  }

}  // namespace stencil::core::script
