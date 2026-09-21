#include "diagnostics.hpp"

#include "parser.hpp"
#include "text.hpp"

#include <algorithm>
#include <cctype>

namespace stencil::core::script {

  namespace {

    constexpr int MAX_EDITS = 2;

    // `toLowerAscii` byte for byte, into a buffer the sweep reuses.
    void lowerInto(std::string_view in, std::string& out) {
      out.clear();
      out.reserve(in.size());
      for (char c : in)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    // The row pair is the caller's, so a sweep over candidates allocates once, not per word.
    int editDistanceInto(std::string_view a, std::string_view b, std::vector<int>& prev,
                         std::vector<int>& cur) {
      const int cap = MAX_EDITS + 1;
      const std::size_t n = a.size(), m = b.size();
      if (n > m + MAX_EDITS || m > n + MAX_EDITS) return cap;

      prev.assign(m + 1, 0);
      cur.assign(m + 1, 0);
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

  }  // namespace

  int editDistance(std::string_view a, std::string_view b) {
    std::vector<int> prev, cur;
    return editDistanceInto(a, b, prev, cur);
  }

  std::string didYouMean(std::string_view word, const std::vector<std::string_view>& candidates) {
    const std::string needle = toLowerAscii(word);
    std::string folded;
    std::vector<int> prev, cur;
    std::string_view best;
    int bestScore = MAX_EDITS + 1;
    for (std::string_view c : candidates) {
      lowerInto(c, folded);
      const int d = editDistanceInto(needle, folded, prev, cur);
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
