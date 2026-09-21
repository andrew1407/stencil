#pragma once
// The op-plan parser suite's sections, one TU each behind this header, called in this order
// from main(). Pure parse checks, so none of them needs a display or a fixture.
#include "opPlan.hpp"
#include "OpSchema.hpp"
#include "../../support/check.hpp"

#include <QString>
#include <QStringList>
#include <cstdio>

namespace llmopplan {

  // §1's one exception: a top-level-only op inside a variant drops THAT variant
  // with a warning — the plan itself survives.
  inline bool variantDropped(const char* json) {
    const auto r = stencil::llm::parseOpPlan(QString::fromUtf8(json));
    return r.ok && r.plan.variants.isEmpty() && r.plan.warnings.size() == 1 &&
           r.plan.warnings[0].startsWith("Dropped variant");
  }

  // The same for an ask-option preview: the option stays, its preview goes.
  inline bool previewDropped(const char* json) {
    const auto r = stencil::llm::parseOpPlan(QString::fromUtf8(json));
    return r.ok && !r.plan.ask.options.isEmpty() &&
           r.plan.ask.options[0].actions.isEmpty() && r.plan.warnings.size() == 1 &&
           r.plan.warnings[0].contains("preview for option");
  }

  void checkExtraction();
  void checkCropSpecs();
  void checkShapeOps();
  void checkPageAndHistory();
  void checkEditorSettings();
  void checkProjectOps();
  void checkEditorRows();
  void checkMultiImageAndVariants();
  void checkAskCards();

}  // namespace llmopplan
