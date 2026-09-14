#include "scriptDiagnostics.hpp"

#include "scriptParser.hpp"
#include "text.hpp"

#include <algorithm>

namespace stencil::core::script {

  namespace {
    constexpr int MAX_EDITS = 2;
  }  // namespace

  int editDistance(std::string_view a, std::string_view b) {
    const int cap = MAX_EDITS + 1;
    const std::size_t n = a.size(), m = b.size();
    if (n > m + MAX_EDITS || m > n + MAX_EDITS) return cap;

    std::vector<int> prev(m + 1), cur(m + 1);
    for (std::size_t j = 0; j <= m; ++j) prev[j] = static_cast<int>(j);
    for (std::size_t i = 1; i <= n; ++i) {
      cur[0] = static_cast<int>(i);
      int best = cur[0];
      for (std::size_t j = 1; j <= m; ++j) {
        const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
        cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        best = std::min(best, cur[j]);
      }
      if (best > MAX_EDITS) return cap;
      prev.swap(cur);
    }
    return std::min(prev[m], cap);
  }

  std::string didYouMean(std::string_view word, const std::vector<std::string_view>& candidates) {
    const std::string needle = toLowerAscii(word);
    std::string_view best;
    int bestScore = MAX_EDITS + 1;
    for (std::string_view c : candidates) {
      const int d = editDistance(needle, toLowerAscii(c));
      if (d < bestScore) {
        bestScore = d;
        best = c;
      }
    }
    return bestScore <= MAX_EDITS ? std::string(best) : std::string();
  }

  Diagnostic makeDiag(Severity sev, const std::string& code, const Token& at,
                      const std::string& message) {
    Diagnostic d;
    d.severity = sev;
    d.code = code;
    d.line = at.line;
    d.col = at.col;
    d.len = at.len;
    d.message = message;
    return d;
  }

  Token tokenOfStmt(const Stmt& st) {
    Token t;
    t.line = st.line;
    t.col = st.col;
    t.len = st.len;
    t.kind = TokenKind::DIRECTIVE;
    t.text = "@" + st.directive;
    return t;
  }

  Diagnostic makeDiag(Severity sev, const std::string& code, const Stmt& at,
                      const std::string& message) {
    return makeDiag(sev, code, tokenOfStmt(at), message);
  }

  Token argErrorToken(const Stmt& st) {
    return st.args.empty() ? tokenOfStmt(st) : st.args[0];
  }

  std::string formatDiagnostic(const std::string& file, const Diagnostic& d) {
    return file + ":" + std::to_string(d.line) + ":" + std::to_string(d.col) + ": " +
           (d.severity == Severity::ERROR ? "error: " : "warning: ") + d.message + " [" +
           d.code + "]";
  }

  bool hasErrors(const std::vector<Diagnostic>& diagnostics) {
    for (const Diagnostic& d : diagnostics)
      if (d.severity == Severity::ERROR) return true;
    return false;
  }

}  // namespace stencil::core::script
