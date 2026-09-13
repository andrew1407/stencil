#pragma once
#include "scriptParser.hpp"

// `@stencil` definitions and `@use stencil …` expansion.
// Port target: browser/js/core/scriptTemplates.js.
namespace stencil::core::script {

  /* Expands one `@use stencil <words> [args…]` statement into the referenced body, with
   * @1..@n replaced by the call's positional arguments. The name is the LONGEST defined
   * template that prefixes the word run; the remaining words are the arguments, so
   * `@use stencil line-style param red cm` resolves name "line-style param", args "red cm".
   * Nested uses expand too, capped at MAX_TEMPLATE_DEPTH; a template that reaches itself
   * is E_TEMPLATE_RECURSION. Returns false when nothing was expanded. */
  bool expandStencilUse(const Stmt& use, std::vector<TemplateDef>& templates, int depth,
                        std::vector<Stmt>& out, std::vector<Diagnostic>& diags);

  // Emits W_UNUSED_TEMPLATE for every definition no `@use stencil` reached.
  void reportUnusedTemplates(const std::vector<TemplateDef>& templates,
                             std::vector<Diagnostic>& diags);

}  // namespace stencil::core::script
