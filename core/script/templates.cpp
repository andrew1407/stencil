#include "templates.hpp"

#include "diagnostics.hpp"
#include "values.hpp"
#include "text.hpp"

#include <algorithm>

namespace stencil::core::script {

  namespace {

    // The call's words, in order, with `stencil` already dropped.
    std::vector<std::string> callWords(const Stmt& use) {
      std::vector<std::string> words;
      bool skippedKeyword = false;
      for (const Token& t : use.args) {
        if (t.kind == TokenKind::PUNCT) continue;
        if (!skippedKeyword) { skippedKeyword = true; continue; }  // the literal "stencil"
        words.push_back(unquoteWord(t.text));
      }
      return words;
    }

    std::string joinRange(const std::vector<std::string>& w, std::size_t n) {
      std::string out;
      for (std::size_t i = 0; i < n; ++i) {
        if (!out.empty()) out.push_back(' ');
        out += w[i];
      }
      return out;
    }

    std::size_t wordCount(const std::string& name) {
      std::size_t n = 1;
      for (char c : name)
        if (c == ' ') ++n;
      return n;
    }

    /* Longest defined name that is a prefix of the word run. The walk starts at the longest
     * name there is, because no longer prefix can match, and shortens the candidate in place:
     * re-joining every prefix made one call cost the SQUARE of its word count. */
    int resolveName(const std::vector<std::string>& words,
                    const std::vector<TemplateDef>& templates, std::size_t& wordsUsed) {
      wordsUsed = 0;
      std::size_t longest = 0;
      for (const TemplateDef& d : templates) longest = std::max(longest, wordCount(d.name));

      std::size_t n = std::min(words.size(), longest);
      std::string candidate = joinRange(words, n);
      for (; n >= 1; --n) {
        for (std::size_t k = 0; k < templates.size(); ++k)
          if (templates[k].name == candidate) {
            wordsUsed = n;
            return static_cast<int>(k);
          }
        candidate.resize(candidate.size() - words[n - 1].size() - (n > 1 ? 1 : 0));
      }
      return -1;
    }

    // @1..@n in the body become the call's arguments; every other token passes through.
    Stmt substitute(const Stmt& body, const std::vector<std::string>& args, bool& badIndex,
                    Token& badAt) {
      Stmt out;
      out.directive = body.directive;
      out.kind = body.kind;
      out.opensBlock = body.opensBlock;
      out.line = body.line;
      out.col = body.col;
      out.len = body.len;
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
        Token filled = t;
        filled.text = args[static_cast<std::size_t>(n - 1)];
        filled.kind = TokenKind::IDENT;
        out.args.push_back(std::move(filled));
      }
      return out;
    }

    bool isNestedStencilUse(const Stmt& st) {
      return st.kind == Directive::USE && !st.args.empty() &&
             toLowerAscii(unquoteWord(st.args[0].text)) == "stencil";
    }

  }  // namespace

  bool expandStencilUse(const Stmt& use, std::vector<TemplateDef>& templates, int depth,
                        int& expansions, std::vector<Stmt>& out,
                        std::vector<Diagnostic>& diags) {
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

    const std::vector<std::string> words = callWords(use);
    if (words.empty()) {
      diags.push_back(makeDiag(Severity::ERROR, "E_ARG_COUNT", use,
                               "'@use stencil' needs a template name"));
      return false;
    }

    std::size_t used = 0;
    const int idx = resolveName(words, templates, used);
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
    const std::vector<std::string> args(words.begin() + static_cast<long>(used), words.end());
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
      Stmt filled = substitute(st, args, badIndex, badAt);
      if (badIndex) {
        diags.push_back(makeDiag(Severity::ERROR, "E_TEMPLATE_PARAM_INDEX", badAt,
                                 "'" + badAt.text + "' is outside this template's " +
                                     std::to_string(arity) + " argument(s)"));
        return false;
      }
      if (isNestedStencilUse(filled)) {
        if (!expandStencilUse(filled, templates, depth + 1, expansions, out, diags))
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
