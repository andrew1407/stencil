#include "scriptParser.hpp"

#include "scriptDiagnostics.hpp"
#include "text.hpp"

#include <cstdlib>

namespace stencil::core::script {

  namespace {

    const std::vector<std::string> kDirectives = {"source", "stencil", "use",  "crop",
                                                  "filter", "line",    "rect", "layout",
                                                  "save",   "frame",   "undo", "redo"};

    bool isNewline(const Token& t) {
      return t.kind == TokenKind::PUNCT && (t.text == "\n" || t.text == ";");
    }

    std::string unquote(const std::string& s) {
      if (s.size() >= 2 && s.front() == '"' && s.back() == '"') return s.substr(1, s.size() - 2);
      return s;
    }

    // The name is every word before the ':' — "lines and rect" is one name.
    std::string joinName(const std::vector<Token>& args) {
      std::string out;
      for (const Token& t : args) {
        if (t.kind == TokenKind::PUNCT) continue;
        if (!out.empty()) out.push_back(' ');
        out += unquote(t.text);
      }
      return out;
    }

    int highestParam(const std::vector<Stmt>& body) {
      int top = 0;
      for (const Stmt& s : body)
        for (const Token& t : s.args)
          if (t.kind == TokenKind::PARAM) {
            const int n = std::atoi(t.text.c_str() + 1);
            if (n > top) top = n;
          }
      return top;
    }

  }  // namespace

  const std::vector<std::string>& directiveNames() { return kDirectives; }

  ParseResult parseScript(const std::vector<Token>& tokens) {
    ParseResult out;
    std::vector<Stmt> stmts;

    // Pass 1 — tokens to statements.
    std::size_t i = 0;
    while (i < tokens.size()) {
      while (i < tokens.size() && (isNewline(tokens[i]) || tokens[i].kind == TokenKind::COMMENT))
        ++i;
      if (i >= tokens.size()) break;

      const Token& head = tokens[i];
      Stmt st;
      st.line = head.line;
      st.col = head.col;

      if (head.kind != TokenKind::DIRECTIVE) {
        out.diagnostics.push_back(makeDiag(Severity::ERROR, "E_BAD_TOKEN", head,
                                           "expected a directive starting with '@', found '" +
                                               head.text + "'"));
        while (i < tokens.size() && !isNewline(tokens[i])) ++i;
        continue;
      }

      st.directive = toLowerAscii(head.text.substr(1));
      st.len = head.len;
      ++i;

      bool unknown = true;
      for (const std::string& d : kDirectives)
        if (d == st.directive) { unknown = false; break; }
      if (unknown) {
        const std::string near = didYouMean(st.directive, kDirectives);
        out.diagnostics.push_back(
            makeDiag(Severity::ERROR, "E_UNKNOWN_DIRECTIVE", head,
                     "unknown directive '@" + st.directive + "'" +
                         (near.empty() ? "" : " — did you mean '@" + near + "'?")));
        while (i < tokens.size() && !isNewline(tokens[i])) ++i;
        continue;
      }

      while (i < tokens.size() && !isNewline(tokens[i])) {
        const Token& t = tokens[i];
        if (t.kind == TokenKind::COMMENT) { ++i; continue; }
        if (t.kind == TokenKind::PUNCT && t.text == ":") {
          // Only a ':' with nothing after it opens a block, so "aspect=3:2" keeps its own.
          std::size_t k = i + 1;
          while (k < tokens.size() && tokens[k].kind == TokenKind::COMMENT) ++k;
          if (k >= tokens.size() || isNewline(tokens[k])) {
            st.opensBlock = true;
            ++i;
            continue;
          }
        }
        if (st.opensBlock)
          out.diagnostics.push_back(makeDiag(Severity::WARNING, "W_TRAILING_TOKENS", t,
                                             "'" + t.text + "' after the block's ':' is ignored"));
        else
          st.args.push_back(t);
        ++i;
      }
      stmts.push_back(st);
    }

    // Pass 2 — statements into blocks and template definitions.
    RawBlock implicitBlock;
    implicitBlock.implicit = true;
    std::vector<RawBlock> sourceBlocks;
    int currentBlock = -1;     // index into sourceBlocks; -1 = the implicit block
    int currentTemplate = -1;  // index into out.templates; -1 = not in a template

    auto tokenAt = [](const Stmt& s) {
      Token t;
      t.line = s.line;
      t.col = s.col;
      t.len = s.len;
      t.text = "@" + s.directive;
      return t;
    };

    // A body that is indented past its header ends at the first statement back at (or
    // left of) the header's column; an unindented body runs to the next block header.
    int bodyColumn = 0, headerColumn = 0;

    for (const Stmt& st : stmts) {
      const bool isHeader = st.directive == "stencil" || st.directive == "source";
      if (!isHeader && bodyColumn > 0 && st.col <= headerColumn) {
        currentTemplate = -1;
        currentBlock = -1;
        bodyColumn = 0;
      }

      if (st.directive == "stencil") {
        currentTemplate = -1;
        if (!st.opensBlock) {
          out.diagnostics.push_back(makeDiag(Severity::ERROR, "E_MISSING_COLON", tokenAt(st),
                                             "'@stencil <name>:' needs a trailing ':'"));
          continue;
        }
        TemplateDef def;
        def.name = joinName(st.args);
        def.line = st.line;
        def.col = st.col;
        def.len = st.len;
        if (def.name.empty()) {
          out.diagnostics.push_back(makeDiag(Severity::ERROR, "E_ARG_COUNT", tokenAt(st),
                                             "'@stencil' needs a name before the ':'"));
          continue;
        }
        bool dup = false;
        for (const TemplateDef& d : out.templates)
          if (d.name == def.name) { dup = true; break; }
        if (dup) {
          out.diagnostics.push_back(makeDiag(Severity::ERROR, "E_DUPLICATE_TEMPLATE", tokenAt(st),
                                             "template '" + def.name + "' is already defined"));
          continue;
        }
        if (static_cast<int>(out.templates.size()) >= MAX_TEMPLATES) {
          out.diagnostics.push_back(makeDiag(Severity::ERROR, "E_LIMIT_TEMPLATES", tokenAt(st),
                                             "too many templates"));
          continue;
        }
        out.templates.push_back(def);
        currentTemplate = static_cast<int>(out.templates.size()) - 1;
        headerColumn = st.col;
        bodyColumn = 0;
        continue;
      }

      if (st.directive == "source") {
        currentTemplate = -1;  // a block header always closes the template above it
        if (!st.opensBlock) {
          out.diagnostics.push_back(makeDiag(Severity::ERROR, "E_MISSING_COLON", tokenAt(st),
                                             "'@source <path|url>:' needs a trailing ':'"));
          continue;
        }
        if (static_cast<int>(sourceBlocks.size()) >= MAX_BLOCKS) {
          out.diagnostics.push_back(
              makeDiag(Severity::ERROR, "E_LIMIT_BLOCKS", tokenAt(st), "too many @source blocks"));
          continue;
        }
        RawBlock b;
        b.header = st;
        b.implicit = false;
        sourceBlocks.push_back(b);
        currentBlock = static_cast<int>(sourceBlocks.size()) - 1;
        currentTemplate = -1;
        headerColumn = st.col;
        bodyColumn = 0;
        continue;
      }

      if (bodyColumn == 0 && (currentTemplate >= 0 || currentBlock >= 0) &&
          st.col > headerColumn)
        bodyColumn = st.col;

      if (currentTemplate >= 0) {
        out.templates[static_cast<std::size_t>(currentTemplate)].body.push_back(st);
        continue;
      }
      if (currentBlock >= 0) sourceBlocks[static_cast<std::size_t>(currentBlock)].body.push_back(st);
      else implicitBlock.body.push_back(st);
    }

    for (TemplateDef& d : out.templates) d.arity = highestParam(d.body);

    if (!implicitBlock.body.empty()) out.blocks.push_back(implicitBlock);
    for (RawBlock& b : sourceBlocks) out.blocks.push_back(b);
    return out;
  }

}  // namespace stencil::core::script
