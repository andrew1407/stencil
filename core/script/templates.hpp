#pragma once
#include "parser.hpp"

// `@stencil` definitions and `@use stencil …` expansion.
// Port target: browser/js/core/script/templates.js.
namespace stencil::core::script {

  // Expands `@use stencil <words> [args…]` into the body of the LONGEST defined name prefixing
  // the run, @1..@n from the rest; nested, bounded by MAX_TEMPLATE_DEPTH / _EXPANSIONS / _OPS.
  bool expandStencilUse(const Stmt& use, std::vector<TemplateDef>& templates, int depth,
                        int& expansions, std::vector<Stmt>& out,
                        std::vector<Diagnostic>& diags);

  // Emits W_UNUSED_TEMPLATE for every definition no `@use stencil` reached.
  void reportUnusedTemplates(const std::vector<TemplateDef>& templates,
                             std::vector<Diagnostic>& diags);

}  // namespace stencil::core::script
