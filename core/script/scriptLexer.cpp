#include "scriptLexer.hpp"

#include "hexNibble.hpp"
#include "text.hpp"

#include <cctype>

namespace stencil::core::script {

  namespace {

    bool isWordByte(char c) {
      return !std::isspace(static_cast<unsigned char>(c)) && c != ',' && c != ';' &&
             c != '#' && c != ':' && c != '(' && c != ')' && c != '=' && c != '"';
    }

    bool allHex(const std::string& s, std::size_t from) {
      for (std::size_t i = from; i < s.size(); ++i)
        if (hexNibble(s[i]) < 0) return false;
      return from < s.size();
    }

    bool looksNumeric(const std::string& s) {
      std::size_t i = (!s.empty() && (s[0] == '-' || s[0] == '+')) ? 1 : 0;
      bool digit = false;
      for (; i < s.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(s[i]))) digit = true;
        else if (s[i] == '.') continue;
        else break;
      }
      return digit;
    }

    // A number may carry a unit; the whole run is one NUMBER token, the suffix a UNIT.
    bool unitSuffix(const std::string& s, std::size_t& unitAt) {
      std::size_t i = (!s.empty() && (s[0] == '-' || s[0] == '+')) ? 1 : 0;
      while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) ++i;
      unitAt = i;
      if (i >= s.size()) return false;
      const std::string u = toLowerAscii(s.substr(i));
      return u == "px" || u == "cm" || u == "mm" || u == "in" || u == "%";
    }

    bool isParamWord(const std::string& s) {
      if (s.size() < 2 || s[0] != '@') return false;
      for (std::size_t i = 1; i < s.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
      return true;
    }

  }  // namespace

  bool isHexColorWord(const std::string& word) {
    if (word.size() < 2 || word[0] != '#') return false;
    const std::size_t n = word.size() - 1;
    if (n != 3 && n != 4 && n != 6 && n != 8) return false;
    return allHex(word, 1);
  }

  LexResult lexScript(const char* text, int len) {
    LexResult out;
    if (!text || len < 0) return out;
    const std::string src(text, static_cast<std::size_t>(len));

    int line = 1, col = 1;
    std::size_t i = 0;
    auto push = [&](TokenKind k, const std::string& t, int atLine, int atCol) {
      if (static_cast<int>(out.tokens.size()) >= MAX_TOKENS) return;
      Token tok;
      tok.line = atLine;
      tok.col = atCol;
      tok.len = static_cast<int>(t.size());
      tok.kind = k;
      tok.text = t;
      out.tokens.push_back(tok);
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
      if (std::isspace(static_cast<unsigned char>(c))) { ++i; ++col; continue; }

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
        push(TokenKind::PUNCT, std::string(1, c), startLine, startCol);
        ++i;
        ++col;
        continue;
      }

      // A word: runs to whitespace or punctuation. '#' only breaks a word when it is
      // not the word's own first byte, so "#ccc" stays whole and "a#b" splits.
      std::size_t j = i;
      std::string word;
      if (c == '#') {
        word.push_back('#');
        ++j;
      }
      while (j < src.size()) {
        // "://" belongs to a URL, so it never breaks the word or opens a block.
        if (src[j] == ':' && j + 2 < src.size() && src[j + 1] == '/' && src[j + 2] == '/') {
          word += "://";
          j += 3;
          continue;
        }
        // So does a port's ':', once the word already carries a scheme — the block's own
        // ':' is never followed by a digit, and "aspect=3:2" carries no scheme.
        if (src[j] == ':' && j + 1 < src.size() && src[j + 1] >= '0' && src[j + 1] <= '9' &&
            word.find("://") != std::string::npos) {
          word.push_back(':');
          ++j;
          continue;
        }
        if (!isWordByte(src[j])) break;
        word.push_back(src[j]);
        ++j;
      }

      if (c == '#' && !isHexColorWord(word)) {
        // A real comment: swallow to end of line, text included.
        std::size_t k = i;
        std::string body;
        while (k < src.size() && src[k] != '\n') body.push_back(src[k++]);
        push(TokenKind::COMMENT, body, startLine, startCol);
        col += static_cast<int>(k - i);
        i = k;
        continue;
      }

      TokenKind kind = TokenKind::IDENT;
      if (word.empty()) {  // a lone '#' or an unexpected byte
        word.push_back(src[i]);
        j = i + 1;
        kind = TokenKind::ERROR;
      } else if (word[0] == '#') {
        kind = TokenKind::COLOR;
      } else if (isParamWord(word)) {
        kind = TokenKind::PARAM;
      } else if (word[0] == '@') {
        kind = TokenKind::DIRECTIVE;
      } else if (looksNumeric(word)) {
        std::size_t unitAt = 0;
        const bool united = unitSuffix(word, unitAt);
        if (united) {
          push(TokenKind::NUMBER, word.substr(0, unitAt), startLine, startCol);
          push(TokenKind::UNIT, word.substr(unitAt), startLine,
               startCol + static_cast<int>(unitAt));
          col += static_cast<int>(j - i);
          i = j;
          continue;
        }
        kind = TokenKind::NUMBER;
      }

      push(kind, word, startLine, startCol);
      col += static_cast<int>(j - i);
      i = j;
    }

    push(TokenKind::PUNCT, "\n", line, col);  // a virtual newline closes the last statement
    return out;
  }

}  // namespace stencil::core::script
