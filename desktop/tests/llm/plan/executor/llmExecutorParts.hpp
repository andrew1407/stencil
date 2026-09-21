#pragma once
// The plan-executor suite's sections, one TU each behind this header, called in this order
// from main(); every one takes the committed 16x12 fixture and the A4 page it is seeded with.
#include "imageOps.hpp"
#include "opPlan.hpp"
#include "planExecutor.hpp"
#include "../../../support/check.hpp"

#include <QColor>
#include <QImage>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace llmexec {

  // Captures the lines the executor hands to the canvas (post-map, post-clamp).
  struct LayoutRecorder : stencil::llm::CanvasPlanTarget {
    using stencil::llm::CanvasPlanTarget::CanvasPlanTarget;
    stencil::core::Lines got;
    void setLayoutLines(const stencil::core::Lines& lines) override {
      got = lines;
      CanvasPlanTarget::setLayoutLines(lines);
    }
  };

  inline bool near(double a, double b) { return std::abs(a - b) < 1e-9; }

  void checkPlanBasics(const QImage& img, const stencil::core::PageSize& a4);
  void checkCoordinateRemapping(const QImage& img, const stencil::core::PageSize& a4);
  void checkEditorSettings(const QImage& img, const stencil::core::PageSize& a4);
  void checkFileOps(const QImage& img, const stencil::core::PageSize& a4);
  void checkProjectOps(const QImage& img, const stencil::core::PageSize& a4);
  void checkImageOps(const QImage& img, const stencil::core::PageSize& a4);
  void checkHistoryOps(const QImage& img, const stencil::core::PageSize& a4);
  void checkAccentOps(const QImage& img, const stencil::core::PageSize& a4);
  void checkProjectRows(const QImage& img, const stencil::core::PageSize& a4);

}  // namespace llmexec
