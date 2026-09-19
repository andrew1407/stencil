// Headless check of the LLM plan executor (src/llm/planExecutor) against the real CanvasWidget, seeded
// with the committed PNG fixture (16x12 solid #3366cc): the llm-contract.md §1-2 execution semantics —
// top-level actions mutate the working image in order, each variant branches from the state AFTER them
// and yields one separate image, `frame` is a plan-level error off-video, and layout coordinates are
// model-frame, re-mapped through earlier crop/rotate and clamped. Runs offscreen.
#include "llmExecutorParts.hpp"

#include <QApplication>

int main(int argc, char** argv) {
  QApplication app(argc, argv);  // offscreen via QT_QPA_PLATFORM

  const QString fixture = QStringLiteral(STENCIL_FIXTURES_DIR "/sample.png");
  QImage img;
  check(img.load(fixture), "fixture PNG loaded");
  check(img.width() == 16 && img.height() == 12, "fixture is 16x12");

  const stencil::core::PageSize a4{21.0, 29.7};

  llmexec::checkPlanBasics(img, a4);
  llmexec::checkCoordinateRemapping(img, a4);
  llmexec::checkEditorSettings(img, a4);
  llmexec::checkFileOps(img, a4);
  llmexec::checkProjectOps(img, a4);
  llmexec::checkImageOps(img, a4);
  llmexec::checkHistoryOps(img, a4);
  llmexec::checkAccentOps(img, a4);
  llmexec::checkProjectRows(img, a4);


  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}