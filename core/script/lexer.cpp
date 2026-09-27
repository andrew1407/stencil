#include "lexer.hpp"

#include "hexNibble.hpp"
#include "values.hpp"
#include "text.hpp"

#include <algorithm>

namespace stencil::core::script {

  namespace {

    // ASCII only: std::isspace follows a locale the host may set, and could split UTF-8.
    bool isSpaceByte(char c) {
      return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    }

    bool isDigitByte(char c) { return c >= '0' && c <= '9'; }

    bool isWordByte(char c) {
      return !isSpaceByte(c) && c != ',' && c != ';' && c != '#' && c != ':' && c != '(' &&
             c != ')' && c != '=' && c != '"';
    }

    bool allHex(std::string_view s, std::size_t from) {
      for (std::size_t i = from; i < s.size(); ++i)
        if (hexNibble(s[i]) < 0) return false;
      return from < s.size();
    }

    // The §1 number, -?\d+(\.\d+)?, as a prefix of `s`: its end, or 0 when there is none.
    std::size_t numberEnd(std::string_view s) {
      std::size_t i = (!s.empty() && s[0] == '-') ? 1 : 0;
      const std::size_t digits = i;
      while (i < s.size() && isDigitByte(s[i])) ++i;
      if (i == digits) return 0;
      if (i + 1 < s.size() && s[i] == '.' && isDigitByte(s[i + 1])) {
        i += 2;
        while (i < s.size() && isDigitByte(s[i])) ++i;
      }
      return i;
    }

    bool isParamWord(std::string_view s) {
      if (s.size() < 2 || s[0] != '@') return false;
      for (std::size_t i = 1; i < s.size(); ++i)
        if (!isDigitByte(s[i])) return false;
      return true;
    }

    // "C:\a.png" and "C:/a.png": a drive letter's ':' belongs to the path, not the block.
    bool isDriveColon(std::string_view src, std::size_t wordStart, std::size_t j) {
      const char d = src[wordStart];
      return j == wordStart + 1 && ((d >= 'A' && d <= 'Z') || (d >= 'a' && d <= 'z')) &&
             j + 1 < src.size() && (src[j + 1] == '\\' || src[j + 1] == '/');
    }

  }  // namespace

  bool isHexColorWord(std::string_view word) {
    if (word.size() < 2 || word[0] != '#') return false;
    const std::size_t n = word.size() - 1;
    if (n != 3 && n != 4 && n != 6 && n != 8) return false;
    return allHex(word, 1);
  }

  WordClass classifyWord(std::string_view word) {
    WordClass out;
    if (word.empty()) return out;
    if (word[0] == '#') {
      if (isHexColorWord(word)) out.kind = TokenKind::COLOR;
    } else if (isParamWord(word)) {
      out.kind = TokenKind::PARAM;
    } else if (word[0] == '@') {
      out.kind = TokenKind::DIRECTIVE;
    } else if (const std::size_t end = numberEnd(word); end > 0) {
      // Any other digit run ("10foo", "1.2.3", "0x10", "1e3") is a word, not a number.
      if (end == word.size()) out.kind = TokenKind::NUMBER;
      else if (isUnitWord(word.substr(end))) out = {TokenKind::NUMBER, end};
    }
    return out;
  }

  LexResult lexScript(const char* text, int len) {
    LexResult out;
    if (!text || len < 0) return out;
    const std::string_view src(text, static_cast<std::size_t>(len));
    // Four bytes a token is the corpus average; growing from nothing copied every string
    // already pushed, over and over, on the way to MAX_TOKENS.
    out.tokens.reserve(std::min<std::size_t>(src.size() / 4 + 1, MAX_TOKENS));

    int line = 1, col = 1;
    std::size_t i = 0;
    bool capped = false;
    auto push = [&](TokenKind k, std::string_view t, int atLine, int atCol) {
      if (static_cast<int>(out.tokens.size()) >= MAX_TOKENS) {
        if (capped) return;
        capped = true;
        out.diagnostics.push_back({Severity::ERROR, "E_LIMIT_TOKENS", atLine, atCol, 0,
                                   "script has too many tokens (over " +
                                       std::to_string(MAX_TOKENS) + ")"});
        return;
      }
      Token tok;
      tok.line = atLine;
      tok.col = atCol;
      tok.len = static_cast<int>(t.size());
      tok.kind = k;
      tok.text = t;
      out.tokens.push_back(std::move(tok));
    };

    while (i < src.size()) {
      const char c = src[i];
      if (c == '\r') { ++i; continue; }
      if (c == '\n') {
        push(TokenKind::PUNCT, "\n", line, col);
        ++i;
        ++line;
        col = 1;
        if (line > MAX_LINES) {
          out.diagnostics.push_back({Severity::ERROR, "E_LIMIT_LINES", line, 1, 0,
                                     "script is too long (over " + std::to_string(MAX_LINES) +
                                         " lines)"});
          return out;
        }
        continue;
      }
      if (isSpaceByte(c)) { ++i; ++col; continue; }

      const int startLine = line, startCol = col;

      if (c == '"') {
        std::string val = "\"";
        std::size_t j = i + 1;
        bool closed = false;
        while (j < src.size() && src[j] != '\n') {
          if (src[j] == '\\' && j + 1 < src.size() && (src[j + 1] == '"' || src[j + 1] == '\\')) {
            val.push_back(src[j + 1]);
            j += 2;
            continue;
          }
          if (src[j] == '"') { closed = true; ++j; break; }
          val.push_back(src[j]);
          ++j;
        }
        if (!closed)
          out.diagnostics.push_back({Severity::ERROR, "E_UNTERMINATED_STRING", startLine,
                                     startCol, static_cast<int>(j - i),
                                     "unterminated string — add a closing quote"});
        val.push_back('"');
        push(TokenKind::STRING, val, startLine, startCol);
        col += static_cast<int>(j - i);
        i = j;
        continue;
      }

      if (c == ',' || c == ':' || c == '(' || c == ')' || c == '=' || c == ';') {
        push(TokenKind::PUNCT, src.substr(i, 1), startLine, startCol);
        ++i;
        ++col;
        continue;
      }

      // A word: runs to whitespace or punctuation. '#' only breaks a word when it is
      // not the word's own first byte, so "#ccc" stays whole and "a#b" splits.
      std::size_t j = i;
      bool hasScheme = false;
      if (c == '#') ++j;
      while (j < src.size()) {
        // "://" belongs to a URL, so it never breaks the word or opens a block.
        if (src[j] == ':' && j + 2 < src.size() && src[j + 1] == '/' && src[j + 2] == '/') {
          hasScheme = true;
          j += 3;
          continue;
        }
        // So does a port's ':', once the word already carries a scheme — the block's own
        // ':' is never followed by a digit, and "aspect=3:2" carries no scheme.
        if (src[j] == ':' && hasScheme && j + 1 < src.size() && isDigitByte(src[j + 1])) {
          ++j;
          continue;
        }
        if (src[j] == ':' && isDriveColon(src, i, j)) {
          ++j;
          continue;
        }
        if (!isWordByte(src[j])) break;
        ++j;
      }
      std::string_view word = src.substr(i, j - i);

      if (c == '#' && !isHexColorWord(word)) {
        // A real comment: swallow to end of line, text included.
        std::size_t k = i;
        while (k < src.size() && src[k] != '\n') ++k;
        push(TokenKind::COMMENT, src.substr(i, k - i), startLine, startCol);
        col += static_cast<int>(k - i);
        i = k;
        continue;
      }

      TokenKind kind = TokenKind::IDENT;
      if (word.empty()) {  // a byte no word may carry, such as a lone ':'
        word = src.substr(i, 1);
        j = i + 1;
        kind = TokenKind::ERROR;
      } else {
        const WordClass wc = classifyWord(word);
        kind = wc.kind;
        if (wc.unitAt != std::string_view::npos) {
          push(TokenKind::NUMBER, word.substr(0, wc.unitAt), startLine, startCol);
          push(TokenKind::UNIT, word.substr(wc.unitAt), startLine,
               startCol + static_cast<int>(wc.unitAt));
          col += static_cast<int>(j - i);
          i = j;
          continue;
        }
      }

      push(kind, word, startLine, startCol);
      col += static_cast<int>(j - i);
      i = j;
    }

    push(TokenKind::PUNCT, "\n", line, col);  // a virtual newline closes the last statement
    return out;
  }

}  // namespace stencil::core::script
