// Headless check of the LLM op-plan parser (src/llm/opPlan) — the desktop port of the llm-contract.md
// §1-2 parse matrix, shared with every client in the contract table: extraction tolerance (fence
// stripping, first balanced object, chat-only fallback), strict known-op validation, unknown-op
// skip-with-warning, and the shared limits (16 actions / 8 variants / 200 lines / 5000 chars / 32
// frame indices). Pure QtCore; no display needed.
#include "llmOpPlanParts.hpp"

#include <QCoreApplication>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  llmopplan::checkExtraction();
  llmopplan::checkCropSpecs();
  llmopplan::checkShapeOps();
  llmopplan::checkPageAndHistory();
  llmopplan::checkEditorSettings();
  llmopplan::checkProjectOps();
  llmopplan::checkEditorRows();
  llmopplan::checkMultiImageAndVariants();
  llmopplan::checkAskCards();

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}