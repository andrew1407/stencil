#pragma once
#include "parser.hpp"

#include <unordered_map>
#include <unordered_set>

// `@stencil` definitions and `@use stencil …` expansion.
// Port target: browser/js/core/script/templates.js.
namespace stencil::core::script {

  /* Built once per script. A call tries at most `longestWords` prefixes, and hashes one only
   * when some name has its byte length, so a long call costs no more than its own words. */
  struct TemplateIndex {
    std::unordered_map<std::string, int> byName;
    std::unordered_set<std::size_t> nameLengths;
    std::size_t longestWords = 0;
  };

  TemplateIndex indexTemplates(const std::vector<TemplateDef>& templates);

  // Expands `@use stencil <words> [args…]` into the body of the LONGEST defined name prefixing
  // the run, @1..@n from the rest; nested, bounded by MAX_TEMPLATE_DEPTH / _EXPANSIONS / _OPS.
  bool expandStencilUse(const Stmt& use, std::vector<TemplateDef>& templates,
                        const TemplateIndex& index, int depth, int& expansions,
                        std::vector<Stmt>& out, std::vector<Diagnostic>& diags);

  // Emits W_UNUSED_TEMPLATE for every definition no `@use stencil` reached.
  void reportUnusedTemplates(const std::vector<TemplateDef>& templates,
                             std::vector<Diagnostic>& diags);

}  // namespace stencil::core::script
