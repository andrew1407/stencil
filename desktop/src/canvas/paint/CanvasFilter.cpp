#include "CanvasWidget.hpp"
#include "../../support/rowWork.hpp"

// The filtered picture paintEvent shows. A large one is rebuilt on the thread pool, so a crop, a
// turn or a filter pick never stalls the GUI thread; a small one stays inline, as cheap as a frame.

namespace stencil::gui {

  namespace {
    // pixels (≈ 2,000 × 2,000): below it the inline rebuild costs about a frame.
    constexpr qint64 FILTER_ASYNC_MIN_PIXELS = 4'000'000;

    struct Built {
      QImage filtered, premul;
    };
  }  // namespace

  void CanvasWidget::refreshFilter() {
    const qint64 pixels = qint64(image.width()) * image.height();
    if (filterMode == core::FilterMode::NONE || pixels < FILTER_ASYNC_MIN_PIXELS) return rebuildFilteredImage();
    // Until the job answers, the last filtered picture stands in only while it has this one's shape.
    if (filteredImage.size() != image.size()) filteredImage = QImage();
    if (filterJobRunning) return;   // one job at a time; its answer repaints, which starts the next
    filterJobRunning = true;
    const QImage src = image;
    const core::FilterMode mode = filterMode;
    const QColor tint = filterColor;
    support::runOnPool<Built>(
        this,
        [src, mode, tint] {
          QImage filtered = filteredPixels(src, mode, tint);
          return Built{filtered, filtered.convertToFormat(QImage::Format_ARGB32_Premultiplied)};
        },
        [this, key = src.cacheKey(), mode, tint](Built built) {
          filterJobRunning = false;
          if (filterDirty && image.cacheKey() == key && filterMode == mode && filterColor == tint) {
            filterDirty = false;
            filteredImage = built.filtered;
            shownPremul.seed(filteredImage, built.premul);
          }
          update();
        });
  }

}  // namespace stencil::gui
