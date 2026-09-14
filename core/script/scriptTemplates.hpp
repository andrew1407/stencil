#pragma once
#include "scriptParser.hpp"

// `@stencil` definitions and `@use stencil …` expansion.
// Port target: browser/js/core/scriptTemplates.js.
namespace stencil::core::script {

  /* Expands one `@use stencil <words> [args…]` into the referenced body, @1..@n filled from
   * the call. The name is the LONGEST defined template prefixing the word run, the rest are
   * arguments. Nested uses expand too, bounded by MAX_TEMPLATE_DEPTH and MAX_OPS.
   * `expansions` counts the whole script's expansion tree against MAX_TEMPLATE_EXPANSIONS. */
  bool expandStencilUse(const Stmt& use, std::vector<TemplateDef>& templates, int depth,
                        int& expansions, std::vector<Stmt>& out,
                        std::vector<Diagnostic>& diags);

  // Emits W_UNUSED_TEMPLATE for every definition no `@use stencil` reached.
  void reportUnusedTemplates(const std::vector<TemplateDef>& templates,
                             std::vector<Diagnostic>& diags);

}  // namespace stencil::core::script
