#include "planExecutor.hpp"

#include "CanvasWidget.hpp"
#include "opRegistry.hpp"

#include <QColor>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace stencil::llm {


  CanvasPlanTarget::CanvasPlanTarget(const QImage& image, const core::PageSize& pageCm)
      : canvas_(std::make_unique<gui::CanvasWidget>()), page_(pageCm) {
    canvas_->setPageCm(page_.width, page_.height);
    if (!image.isNull()) {
      // Adopt the snapshot 1:1 — a FULL-frame crop, so a variant starts from
      // exactly the working image (no default page-aspect auto-crop).
      canvas_->loadFromImage(
          image,
          core::CropRect{0, 0, static_cast<double>(image.width()),
                         static_cast<double>(image.height())},
          0);
    }
  }

  CanvasPlanTarget::~CanvasPlanTarget() = default;

  bool CanvasPlanTarget::hasImage() const { return canvas_->hasImage(); }

  QSize CanvasPlanTarget::effectiveOriginalSize() const {
    return canvas_->effectiveOriginalImage().size();
  }

  QSize CanvasPlanTarget::workingSize() const { return canvas_->image().size(); }

  bool CanvasPlanTarget::applyCropRect(const core::CropRect& rect) {
    canvas_->applyCrop(rect, /*recalc=*/true);
    return true;
  }

  void CanvasPlanTarget::rotateQuarter(bool clockwise) { canvas_->rotateImage(clockwise); }

  void CanvasPlanTarget::setImageFilter(const QString& mode, const QString& tintHex) {
    if (tintHex.isEmpty()) canvas_->setFilter(mode);
    else canvas_->setImageFilter(mode, QColor(tintHex));
  }

  void CanvasPlanTarget::setLayoutLines(const core::Lines& lines) {
    canvas_->setLines(lines);
  }

  void CanvasPlanTarget::setFormula(QChar axis, const QString& expr) {
    (axis == QLatin1Char('x') ? formulaX : formulaY) = expr;
  }

  void CanvasPlanTarget::setPageFormat(const QString& isoName) {
    pageFormat = isoName;
    const core::PageSize ps = core::namedPageSize(isoName.toStdString());
    if (ps.width > 0) {
      page_ = ps;
      canvas_->setPageCm(page_.width, page_.height);
    }
  }

  bool CanvasPlanTarget::hasDrawnLines() const { return !canvas_->lines().empty(); }

  QImage CanvasPlanTarget::renderResult() const {
    return canvas_->renderToImage(/*withOverlay=*/true);
  }
}  // namespace stencil::llm

