#include "templates.hpp"

#include "diagnostics.hpp"
#include "lexer.hpp"
#include "values.hpp"
#include "text.hpp"

#include <algorithm>

namespace stencil::core::script {

  namespace {

    // The call's words, in order, with `stencil` already dropped; a length stays one word.
    std::vector<Token> callWords(const Stmt& use) {
      std::vector<Token> words = gluedWords(use.args);
      if (!words.empty()) words.erase(words.begin());  // the literal "stencil"
      return words;
    }

    std::string joinRange(const std::vector<Token>& w, std::size_t n) {
      std::string out;
      for (std::size_t i = 0; i < n; ++i) {
        if (i > 0) out.push_back(' ');  // as JS join: an empty word keeps its separator
        out += unquoteWord(w[i].text);
      }
      return out;
    }

    // Longest defined name prefixing the word run; the candidate shrinks in place.
    int resolveName(const std::vector<Token>& words, const TemplateIndex& index,
                    std::size_t& wordsUsed) {
      wordsUsed = 0;
      std::size_t n = std::min(words.size(), index.longestWords);
      std::string candidate = joinRange(words, n);
      for (; n >= 1; --n) {
        if (index.nameLengths.count(candidate.size()) > 0) {
          const auto hit = index.byName.find(candidate);
          if (hit != index.byName.end()) {
            wordsUsed = n;
            return hit->second;
          }
        }
        const std::size_t drop = unquoteWord(words[n - 1].text).size() + (n > 1 ? 1 : 0);
        candidate.resize(candidate.size() - drop);
      }
      return -1;
    }

    /* @n becomes the n-th argument, re-read by the lexer's word rules so "10%" is a NUMBER
     * and a UNIT again; a quoted argument stays one plain word. */
    void fill(const Token& param, const Token& arg, std::vector<Token>& out) {
      Token filled = param;
      filled.text = unquoteWord(arg.text);
      filled.kind = TokenKind::IDENT;
      if (arg.kind != TokenKind::STRING) {
        const WordClass wc = classifyWord(arg.text);
        if (wc.kind == TokenKind::NUMBER || wc.kind == TokenKind::COLOR) filled.kind = wc.kind;
        if (wc.unitAt != std::string_view::npos) {
          Token unit = param;
          unit.kind = TokenKind::UNIT;
          unit.text = arg.text.substr(wc.unitAt);
          unit.col = param.col + static_cast<int>(wc.unitAt);
          unit.len = static_cast<int>(unit.text.size());
          filled.text.resize(wc.unitAt);
          filled.len = static_cast<int>(wc.unitAt);
          out.push_back(std::move(filled));
          out.push_back(std::move(unit));
          return;
        }
      }
      out.push_back(std::move(filled));
    }

    // Every other token passes through; the copy remembers the outermost call that made it.
    Stmt substitute(const Stmt& body, const Stmt& use, const std::vector<Token>& args,
                    bool& badIndex, Token& badAt) {
      Stmt out;
      out.directive = body.directive;
      out.kind = body.kind;
      out.opensBlock = body.opensBlock;
      out.line = body.line;
      out.col = body.col;
      out.len = body.len;
      out.callLine = use.callLine > 0 ? use.callLine : use.line;
      out.callCol = use.callLine > 0 ? use.callCol : use.col;
      out.callLen = use.callLine > 0 ? use.callLen : use.len;
      out.args.reserve(body.args.size());
      for (const Token& t : body.args) {
        if (t.kind != TokenKind::PARAM) {
          out.args.push_back(t);
          continue;
        }
        const int n = parseIntClamped(t.text.substr(1));
        if (n < 1 || n > static_cast<int>(args.size())) {
          badIndex = true;
          badAt = t;
          continue;
        }
        fill(t, args[static_cast<std::size_t>(n - 1)], out.args);
      }
      return out;
    }

    bool isNestedStencilUse(const Stmt& st) {
      return st.kind == Directive::USE && !st.args.empty() &&
             toLowerAscii(unquoteWord(st.args[0].text)) == "stencil";
    }

    std::size_t wordCount(const std::string& name) {
      std::size_t n = 1;
      for (char c : name)
        if (c == ' ') ++n;
      return n;
    }

  }  // namespace

  TemplateIndex indexTemplates(const std::vector<TemplateDef>& templates) {
    TemplateIndex index;
    for (std::size_t k = 0; k < templates.size(); ++k) {
      index.byName.emplace(templates[k].name, static_cast<int>(k));
      index.nameLengths.insert(templates[k].name.size());
      index.longestWords = std::max(index.longestWords, wordCount(templates[k].name));
    }
    return index;
  }

  bool expandStencilUse(const Stmt& use, std::vector<TemplateDef>& templates,
                        const TemplateIndex& index, int depth, int& expansions,
                        std::vector<Stmt>& out, std::vector<Diagnostic>& diags) {
    if (depth > MAX_TEMPLATE_DEPTH) {
      diags.push_back(makeDiag(Severity::ERROR, "E_TEMPLATE_RECURSION", use,
                               "templates nest more than " + std::to_string(MAX_TEMPLATE_DEPTH) +
                                   " deep — is one using itself?"));
      return false;
    }
    /* An expansion that yields no statement is still work, and nesting multiplies it: bodies
     * of nothing but nested uses reach neither the op cap below nor the depth cap above. */
    if (++expansions > MAX_TEMPLATE_EXPANSIONS) {
      diags.push_back(
          makeDiag(Severity::ERROR, "E_LIMIT_OPS", use, "the script has too many ops"));
      return false;
    }

    const std::vector<Token> words = callWords(use);
    if (words.empty()) {
      diags.push_back(makeDiag(Severity::ERROR, "E_ARG_COUNT", use,
                               "'@use stencil' needs a template name"));
      return false;
    }

    std::size_t used = 0;
    const int idx = resolveName(words, index, used);
    if (idx < 0) {
      std::vector<std::string_view> names;
      for (const TemplateDef& d : templates) names.push_back(d.name);
      const std::string all = joinRange(words, words.size());
      const std::string near = didYouMean(all, names);
      diags.push_back(makeDiag(Severity::ERROR, "E_UNDEFINED_TEMPLATE", use,
                               "no template named '" + all + "'" +
                                   (near.empty() ? "" : " — did you mean '" + near + "'?")));
      return false;
    }

    TemplateDef& def = templates[static_cast<std::size_t>(idx)];
    const std::vector<Token> args(words.begin() + static_cast<long>(used), words.end());
    const int arity = def.arity;
    if (static_cast<int>(args.size()) != arity) {
      diags.push_back(makeDiag(Severity::ERROR, "E_TEMPLATE_ARITY", use,
                               "template '" + def.name + "' takes " + std::to_string(arity) +
                                   " argument(s), got " + std::to_string(args.size())));
      return false;
    }

    def.used = true;
    // Iterated in place: expansion only flips `used`, so no recursion can resize `templates`.
    for (const Stmt& st : def.body) {
      bool badIndex = false;
      Token badAt;
      Stmt filled = substitute(st, use, args, badIndex, badAt);
      if (badIndex) {
        diags.push_back(makeDiag(Severity::ERROR, "E_TEMPLATE_PARAM_INDEX", badAt,
                                 "'" + badAt.text + "' is outside this template's " +
                                     std::to_string(arity) + " argument(s)"));
        return false;
      }
      if (isNestedStencilUse(filled)) {
        if (!expandStencilUse(filled, templates, index, depth + 1, expansions, out, diags))
          return false;
        continue;
      }
      // The fan-out is bounded here, before the statements exist: nested uses multiply.
      if (static_cast<int>(out.size()) >= MAX_OPS) {
        diags.push_back(
            makeDiag(Severity::ERROR, "E_LIMIT_OPS", use, "the script has too many ops"));
        return false;
      }
      out.push_back(std::move(filled));
    }
    return true;
  }

  void reportUnusedTemplates(const std::vector<TemplateDef>& templates,
                             std::vector<Diagnostic>& diags) {
    for (const TemplateDef& d : templates) {
      if (d.used) continue;
      Token t;
      t.line = d.line;
      t.col = d.col;
      t.len = d.len;
      diags.push_back(makeDiag(Severity::WARNING, "W_UNUSED_TEMPLATE", t,
                               "template '" + d.name + "' is never used"));
    }
  }

}  // namespace stencil::core::script
