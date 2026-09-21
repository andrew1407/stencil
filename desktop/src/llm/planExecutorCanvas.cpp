#include "planExecutor.hpp"

#include "CanvasWidget.hpp"
#include "opRegistry.hpp"

#include <QColor>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace stencil::llm {


  CanvasPlanTarget::CanvasPlanTarget(const QImage& image, const core::PageSize& pageCm)
      : canvas(std::make_unique<gui::CanvasWidget>()), page(pageCm) {
    canvas->setPageCm(page.width, page.height);
    if (!image.isNull()) {
      // Adopt the snapshot 1:1 — a FULL-frame crop, so a variant starts from
      // exactly the working image (no default page-aspect auto-crop).
      canvas->loadFromImage(
          image,
          core::CropRect{0, 0, static_cast<double>(image.width()),
                         static_cast<double>(image.height())},
          0);
    }
  }

  CanvasPlanTarget::~CanvasPlanTarget() = default;

  bool CanvasPlanTarget::hasImage() const { return canvas->hasImage(); }

  QSize CanvasPlanTarget::effectiveOriginalSize() const {
    return canvas->effectiveOriginalImage().size();
  }

  QSize CanvasPlanTarget::workingSize() const { return canvas->getImage().size(); }

  bool CanvasPlanTarget::applyCropRect(const core::CropRect& rect) {
    canvas->applyCrop(rect, /*recalc=*/true);
    return true;
  }

  void CanvasPlanTarget::rotateQuarter(bool clockwise) { canvas->rotateImage(clockwise); }

  void CanvasPlanTarget::setImageFilter(const QString& mode, const QString& tintHex) {
    if (tintHex.isEmpty()) canvas->setFilter(mode);
    else canvas->setImageFilter(mode, QColor(tintHex));
  }

  void CanvasPlanTarget::setLayoutLines(const core::Lines& lines) {
    canvas->setLines(lines);
  }

  void CanvasPlanTarget::commitLayoutLines(const core::Lines& lines) {
    canvas->commitLines(lines);
  }

  bool CanvasPlanTarget::captureEdit(EditState& out) const {
    out.valid = true;
    out.crop = canvas->getCropRect();
    out.filterMode = canvas->getImageFilter();
    out.filterTint = canvas->getFilterColor().name();
    out.lines = canvas->getLines();
    return true;
  }

  void CanvasPlanTarget::setFormula(QChar axis, const QString& expr) {
    (axis == QLatin1Char('x') ? formulaX : formulaY) = expr;
  }

  void CanvasPlanTarget::setPageFormat(const QString& isoName) {
    pageFormat = isoName;
    const core::PageSize ps = core::namedPageSize(isoName.toStdString());
    if (ps.width > 0) {
      page = ps;
      canvas->setPageCm(page.width, page.height);
    }
  }

  bool CanvasPlanTarget::hasDrawnLines() const { return !canvas->getLines().empty(); }

  QImage CanvasPlanTarget::renderResult() const {
    return canvas->renderToImage(/*withOverlay=*/true);
  }
}  // namespace stencil::llm

