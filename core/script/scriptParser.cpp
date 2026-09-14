#include "scriptParser.hpp"

#include "scriptDiagnostics.hpp"
#include "scriptValues.hpp"
#include "text.hpp"

namespace stencil::core::script {

  namespace {

    bool isNewline(const Token& t) {
      return t.kind == TokenKind::PUNCT && (t.text == "\n" || t.text == ";");
    }

    Directive resolveDirective(const std::string& word) {
      for (const DirectiveWord& d : DIRECTIVE_WORDS)
        if (d.word == word) return d.kind;
      return Directive::NONE;
    }

    int highestParam(const std::vector<Stmt>& body) {
      int top = 0;
      for (const Stmt& s : body)
        for (const Token& t : s.args)
          if (t.kind == TokenKind::PARAM) {
            const int n = parseIntClamped(t.text.substr(1));
            if (n > top) top = n;
          }
      return top;
    }

  }  // namespace

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
      st.kind = resolveDirective(st.directive);
      st.len = head.len;
      ++i;

      if (st.kind == Directive::NONE) {
        std::vector<std::string_view> words;
        for (const DirectiveWord& d : DIRECTIVE_WORDS) words.push_back(d.word);
        const std::string near = didYouMean(st.directive, words);
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
      stmts.push_back(std::move(st));
    }

    // Pass 2 — statements into blocks and template definitions.
    RawBlock implicitBlock;
    implicitBlock.implicit = true;
    std::vector<RawBlock> sourceBlocks;
    int currentBlock = -1;     // index into sourceBlocks; -1 = the implicit block
    int currentTemplate = -1;  // index into out.templates; -1 = not in a template

    // A body that is indented past its header ends at the first statement back at (or
    // left of) the header's column; an unindented body runs to the next block header.
    int bodyColumn = 0, headerColumn = 0;

    for (Stmt& st : stmts) {
      const bool isHeader = st.kind == Directive::STENCIL || st.kind == Directive::SOURCE;
      if (!isHeader && bodyColumn > 0 && st.col <= headerColumn) {
        currentTemplate = -1;
        currentBlock = -1;
        bodyColumn = 0;
      }

      if (st.kind == Directive::STENCIL) {
        currentTemplate = -1;
        if (!st.opensBlock) {
          out.diagnostics.push_back(makeDiag(Severity::ERROR, "E_MISSING_COLON", st,
                                             "'@stencil <name>:' needs a trailing ':'"));
          continue;
        }
        TemplateDef def;
        def.name = joinWords(st.args);  // every word before the ':' — "lines and rect" is one name
        def.line = st.line;
        def.col = st.col;
        def.len = st.len;
        if (def.name.empty()) {
          out.diagnostics.push_back(makeDiag(Severity::ERROR, "E_ARG_COUNT", st,
                                             "'@stencil' needs a name before the ':'"));
          continue;
        }
        bool isDuplicate = false;
        for (const TemplateDef& d : out.templates)
          if (d.name == def.name) { isDuplicate = true; break; }
        if (isDuplicate) {
          out.diagnostics.push_back(makeDiag(Severity::ERROR, "E_DUPLICATE_TEMPLATE", st,
                                             "template '" + def.name + "' is already defined"));
          continue;
        }
        if (static_cast<int>(out.templates.size()) >= MAX_TEMPLATES) {
          out.diagnostics.push_back(makeDiag(Severity::ERROR, "E_LIMIT_TEMPLATES", st,
                                             "too many templates"));
          continue;
        }
        out.templates.push_back(std::move(def));
        currentTemplate = static_cast<int>(out.templates.size()) - 1;
        headerColumn = st.col;
        bodyColumn = 0;
        continue;
      }

      if (st.kind == Directive::SOURCE) {
        currentTemplate = -1;  // a block header always closes the template above it
        if (!st.opensBlock) {
          out.diagnostics.push_back(makeDiag(Severity::ERROR, "E_MISSING_COLON", st,
                                             "'@source <path|url>:' needs a trailing ':'"));
          continue;
        }
        if (static_cast<int>(sourceBlocks.size()) >= MAX_BLOCKS) {
          out.diagnostics.push_back(
              makeDiag(Severity::ERROR, "E_LIMIT_BLOCKS", st, "too many @source blocks"));
          continue;
        }
        RawBlock& block = sourceBlocks.emplace_back();
        block.header = std::move(st);
        block.implicit = false;
        currentBlock = static_cast<int>(sourceBlocks.size()) - 1;
        currentTemplate = -1;
        headerColumn = block.header.col;
        bodyColumn = 0;
        continue;
      }

      if (bodyColumn == 0 && (currentTemplate >= 0 || currentBlock >= 0) &&
          st.col > headerColumn)
        bodyColumn = st.col;

      if (currentTemplate >= 0) {
        out.templates[static_cast<std::size_t>(currentTemplate)].body.push_back(std::move(st));
        continue;
      }
      if (currentBlock >= 0)
        sourceBlocks[static_cast<std::size_t>(currentBlock)].body.push_back(std::move(st));
      else
        implicitBlock.body.push_back(std::move(st));
    }

    for (TemplateDef& d : out.templates) d.arity = highestParam(d.body);

    if (!implicitBlock.body.empty()) out.blocks.push_back(std::move(implicitBlock));
    for (RawBlock& b : sourceBlocks) out.blocks.push_back(std::move(b));
    return out;
  }

}  // namespace stencil::core::script
