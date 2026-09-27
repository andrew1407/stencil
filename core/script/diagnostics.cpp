#include "diagnostics.hpp"

#include "parser.hpp"
#include "text.hpp"

#include <algorithm>
#include <cctype>

namespace stencil::core::script {

  namespace {

    constexpr int MAX_EDITS = 2;

    // An ASCII fold, byte for byte, into a buffer the sweep reuses: std::tolower follows a
    // locale the host may set, and diagnostics.js folds only A-Z too.
    void lowerInto(std::string_view in, std::string& out) {
      out.clear();
      out.reserve(in.size());
      for (char c : in) out.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
    }

    // Past this a name gets no suggestion: nobody mistypes a word that long by two letters.
    constexpr std::size_t MAX_SUGGEST_LENGTH = 64;

    /* Only the diagonal band |i - j| <= MAX_EDITS can hold a distance under the cap, so only
     * it is filled; the rows are the caller's, so a sweep allocates once, not per word. */
    int editDistanceInto(std::string_view a, std::string_view b, std::vector<int>& prev,
                         std::vector<int>& cur) {
      const int cap = MAX_EDITS + 1;
      const std::size_t n = a.size(), m = b.size();
      if (n > m + MAX_EDITS || m > n + MAX_EDITS) return cap;
      if (n > MAX_SUGGEST_LENGTH || m > MAX_SUGGEST_LENGTH) return cap;

      const std::size_t band = MAX_EDITS;
      prev.assign(m + 2, cap);
      cur.assign(m + 2, cap);
      for (std::size_t j = 0; j <= std::min(m, band); ++j) prev[j] = static_cast<int>(j);
      for (std::size_t i = 1; i <= n; ++i) {
        const std::size_t lo = i > band ? i - band : 1;
        const std::size_t hi = std::min(m, i + band);
        cur[lo - 1] = lo == 1 ? std::min(static_cast<int>(i), cap) : cap;
        int best = cur[lo - 1];
        for (std::size_t j = lo; j <= hi; ++j) {
          const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
          cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost, cap});
          best = std::min(best, cur[j]);
        }
        cur[hi + 1] = cap;
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
    std::string needle, folded;
    lowerInto(word, needle);
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

  void noteCallSite(std::vector<Diagnostic>& diags, std::size_t from, CallSite call,
                    std::set<std::string>& seen) {
    std::size_t keep = from;
    for (std::size_t k = from; k < diags.size(); ++k) {
      Diagnostic d = std::move(diags[k]);
      if (d.line != call.line || d.col != call.col) {
        const std::string key = d.code + ":" + std::to_string(d.line) + ":" + std::to_string(d.col);
        if (!seen.insert(key).second) continue;
        d.message += " (from the @use stencil at " + std::to_string(call.line) + ":" +
                     std::to_string(call.col) + ")";
        d.relatedLine = call.line;
        d.relatedCol = call.col;
        d.relatedLen = call.len;
      }
      diags[keep++] = std::move(d);
    }
    diags.resize(keep);
  }

}  // namespace stencil::core::script
