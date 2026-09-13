#include "scriptTemplates.hpp"

#include "scriptArgs.hpp"
#include "scriptDiagnostics.hpp"

#include <cstdlib>

namespace stencil::core::script {

  namespace {

    Token tokenOf(const Stmt& s) {
      Token t;
      t.line = s.line;
      t.col = s.col;
      t.len = s.len;
      t.text = "@" + s.directive;
      return t;
    }

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

    // Longest defined name that is a prefix of the word run.
    int resolveName(const std::vector<std::string>& words,
                    const std::vector<TemplateDef>& templates, std::size_t& wordsUsed) {
      int best = -1;
      wordsUsed = 0;
      for (std::size_t n = words.size(); n >= 1; --n) {
        const std::string candidate = joinRange(words, n);
        for (std::size_t k = 0; k < templates.size(); ++k)
          if (templates[k].name == candidate) {
            wordsUsed = n;
            return static_cast<int>(k);
          }
      }
      return best;
    }

    // @1..@n in the body become the call's arguments; every other token passes through.
    Stmt substitute(const Stmt& body, const std::vector<std::string>& args, bool& badIndex,
                    Token& badAt) {
      Stmt out = body;
      out.args.clear();
      for (const Token& t : body.args) {
        if (t.kind != TokenKind::PARAM) {
          out.args.push_back(t);
          continue;
        }
        const int n = std::atoi(t.text.c_str() + 1);
        if (n < 1 || n > static_cast<int>(args.size())) {
          badIndex = true;
          badAt = t;
          continue;
        }
        Token filled = t;
        filled.text = args[static_cast<std::size_t>(n - 1)];
        filled.kind = TokenKind::IDENT;
        out.args.push_back(filled);
      }
      return out;
    }

  }  // namespace

  bool expandStencilUse(const Stmt& use, std::vector<TemplateDef>& templates, int depth,
                        std::vector<Stmt>& out, std::vector<Diagnostic>& diags) {
    if (depth > MAX_TEMPLATE_DEPTH) {
      diags.push_back(makeDiag(Severity::ERROR, "E_TEMPLATE_RECURSION", tokenOf(use),
                               "templates nest more than " + std::to_string(MAX_TEMPLATE_DEPTH) +
                                   " deep — is one using itself?"));
      return false;
    }

    const std::vector<std::string> words = callWords(use);
    if (words.empty()) {
      diags.push_back(makeDiag(Severity::ERROR, "E_ARG_COUNT", tokenOf(use),
                               "'@use stencil' needs a template name"));
      return false;
    }

    std::size_t used = 0;
    const int idx = resolveName(words, templates, used);
    if (idx < 0) {
      std::vector<std::string> names;
      for (const TemplateDef& d : templates) names.push_back(d.name);
      const std::string near = didYouMean(joinRange(words, words.size()), names);
      diags.push_back(makeDiag(Severity::ERROR, "E_UNDEFINED_TEMPLATE", tokenOf(use),
                               "no template named '" + joinRange(words, words.size()) + "'" +
                                   (near.empty() ? "" : " — did you mean '" + near + "'?")));
      return false;
    }

    const std::vector<std::string> args(words.begin() + static_cast<long>(used), words.end());
    const int arity = templates[static_cast<std::size_t>(idx)].arity;
    if (static_cast<int>(args.size()) != arity) {
      diags.push_back(makeDiag(Severity::ERROR, "E_TEMPLATE_ARITY", tokenOf(use),
                               "template '" + templates[static_cast<std::size_t>(idx)].name +
                                   "' takes " + std::to_string(arity) + " argument(s), got " +
                                   std::to_string(args.size())));
      return false;
    }

    templates[static_cast<std::size_t>(idx)].used = true;
    // Copy the body before recursing: expansion may mark other templates used, and a
    // nested @use must not see a half-substituted parent.
    const std::vector<Stmt> body = templates[static_cast<std::size_t>(idx)].body;

    for (const Stmt& st : body) {
      bool badIndex = false;
      Token badAt;
      Stmt filled = substitute(st, args, badIndex, badAt);
      if (badIndex) {
        diags.push_back(makeDiag(Severity::ERROR, "E_TEMPLATE_PARAM_INDEX", badAt,
                                 "'" + badAt.text + "' is outside this template's " +
                                     std::to_string(arity) + " argument(s)"));
        return false;
      }
      if (filled.directive == "use" && !filled.args.empty() &&
          unquoteWord(filled.args[0].text) == "stencil") {
        if (!expandStencilUse(filled, templates, depth + 1, out, diags)) return false;
        continue;
      }
      out.push_back(filled);
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
