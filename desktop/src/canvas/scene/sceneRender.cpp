#include "CanvasScene.hpp"
#include "canvasPaintCache.hpp"
#include "markMetrics.hpp"
#include "../../support/rowWork.hpp"

#include <QFileInfo>
#include <QPainter>
#include <QPen>

#include <cstdint>

// Rendering the scene off screen (the export variants), the filtered picture they start from and
// the compare split both they and the live view draw.

namespace stencil::gui {

  // Native-resolution render of an export variant. Mirrors the browser's export/service.js
  // renderExportCanvas / renderSplitExportCanvas op-for-op.
  QImage CanvasScene::renderToImage(const QString& variant, bool withDivider) const {
    if (image.isNull()) return QImage();

    if (variant == "original") {
      // The cropped+rotated original alone — no filter, no annotations.
      return image.convertToFormat(QImage::Format_ARGB32);
    }

    // Resolve the filtered pixels at native size (const-safe: we don't touch the
    // cached filteredImage/filterDirty here, we recompute locally if needed).
    QImage base;
    if (filterMode == core::FilterMode::NONE) {
      base = image.convertToFormat(QImage::Format_ARGB32);
    } else if (!filterDirty && !filteredImage.isNull()) {
      base = filteredImage;  // cache already current
    } else {
      // Recompute via a temporary canvas to keep this method const.
      CanvasScene* self = const_cast<CanvasScene*>(this);
      self->rebuildFilteredImage();
      base = filteredImage.isNull()
                 ? image.convertToFormat(QImage::Format_ARGB32)
                 : filteredImage;
    }

    QImage out = base.convertToFormat(QImage::Format_ARGB32);
    if (variant == "tint") return out;  // filtered, no annotations

    // "current" and "split" draw the visible lines/points at native scale - no hover/selection rings
    // (highlight = false) - honouring the show flags (drawLineScaled checks them itself).
    {
      QPainter p(&out);
      p.setRenderHint(QPainter::Antialiasing, true);
      for (int i = 0; i < static_cast<int>(lines.size()); ++i)
        drawLineScaled(p, lines[i], i, 1.0, /*highlight=*/false, /*live=*/nullptr);
      drawLineScaled(p, currentLine, -1, 1.0, /*highlight=*/false, /*live=*/nullptr);
    }
    if (variant == "split") {
      QPainter p(&out);
      p.setRenderHint(QPainter::Antialiasing, true);
      const CompareMode mode = compareMode == CompareMode::HORIZONTAL ? CompareMode::HORIZONTAL
                                                                       : CompareMode::VERTICAL;
      paintCompareSplit(p, mode, /*scale=*/1.0, withDivider);
    }
    return out;
  }

  QImage CanvasScene::renderToImage(bool withOverlay) const {
    return renderToImage(withOverlay ? QStringLiteral("current") : QStringLiteral("tint"));
  }

  std::shared_ptr<CanvasScene> CanvasScene::renderCopy() const {
    auto copy = std::make_shared<CanvasScene>();
    copy->image = image;
    copy->originalImage = originalImage;
    copy->cropRect = cropRect;
    copy->rotationQuarters = rotationQuarters;
    copy->imagePath = imagePath;
    copy->lines = lines;
    copy->currentLine = currentLine;
    copy->showPoints = showPoints;
    copy->showLines = showLines;
    copy->dark = dark;
    copy->accentKey = accentKey;
    copy->selGlow = selGlow;
    copy->hoverRing = hoverRing;
    copy->focusRing = focusRing;
    copy->imageFilter = imageFilter;
    copy->filterMode = filterMode;
    copy->filterColor = filterColor;
    copy->filteredImage = filteredImage;
    copy->filterDirty = filterDirty;
    copy->compareMode = compareMode;
    copy->compareSplit = compareSplit;
    copy->blankPage = blankPage;
    copy->pinnedPalette = std::make_shared<const Palette>(paintPalette(dark, accentKey, selGlow, hoverRing));
    return copy;
  }

  QString CanvasScene::imageBaseName() const {
    if (imagePath.isEmpty()) return QStringLiteral("image");
    return QFileInfo(imagePath).completeBaseName();
  }

  QString CanvasScene::imageExt() const {
    if (imagePath.isEmpty()) return QStringLiteral("png");
    const QString suffix = QFileInfo(imagePath).suffix();
    return suffix.isEmpty() ? QStringLiteral("png") : suffix;
  }

  // The pixel math lives once in core (shared with the wasm build); the row split is the adapter's.
  // Format_RGBA8888 is core's byte order and tightly packed, so a row starts at y * width * 4.
  void CanvasScene::rebuildFilteredImage() {
    filterDirty = false;
    if (image.isNull() || filterMode == core::FilterMode::NONE) {
      filteredImage = QImage();
      return;
    }
    QImage img = image.convertToFormat(QImage::Format_RGBA8888);
    const int w = img.width();
    const int h = img.height();
    std::uint8_t* bits = img.bits();
    // Enough rows that a slice outweighs handing it to another thread.
    constexpr int MIN_ROWS_PER_SLICE = 64;
    if (filterMode == core::FilterMode::CONTOUR) {
      // Sobel reads one row OUTSIDE its range each side, so every luma row must exist before any
      // sobel slice runs: two phases, never interleaved per tile (core/raster/imageFilter.hpp).
      std::vector<std::uint8_t> luma(static_cast<std::size_t>(w) * h);
      support::forEachSlice(h, MIN_ROWS_PER_SLICE, [&](int y0, int y1) {
        core::buildLumaRows(bits, w, h, y0, y1, luma.data());
      });
      support::forEachSlice(h, MIN_ROWS_PER_SLICE, [&](int y0, int y1) {
        core::sobelRows(luma.data(), bits, w, h, y0, y1);
      });
      filteredImage = img;
      return;
    }
    // The tint channels are read only for the custom mode.
    const int tr = filterColor.red();
    const int tg = filterColor.green();
    const int tb = filterColor.blue();
    support::forEachSlice(h, MIN_ROWS_PER_SLICE, [&](int y0, int y1) {
      core::applyFilterRows(filterMode, bits, w, y0, y1, tr, tg, tb);
    });
    filteredImage = img;
  }

  // The "original" side: raw pixels, but a BLANK page as currently coloured.
  const QImage& CanvasScene::compareBaseImage() const {
    return blankPage && filterMode != core::FilterMode::NONE && !filteredImage.isNull() ? filteredImage
                                                                                        : image;
  }

  // Divider metrics are in widget space: constant on-screen thickness at any zoom.
  void CanvasScene::paintCompareSplit(QPainter& p, CompareMode mode, double scale, bool withDivider) const {
    const double w = image.width() * scale;
    const double h = image.height() * scale;
    const double f = std::clamp(compareSplit, 0.0, 1.0);

    p.save();
    p.setClipRect(mode == CompareMode::VERTICAL ? QRectF(0, 0, w * f, h) : QRectF(0, 0, w, h * f));
    // A blank page keeps its fill + tint (its colour IS the page); a picture drops the filter too.
    p.drawImage(QRectF(0, 0, w, h), basePremul.of(compareBaseImage()));
    p.restore();

    if (!withDivider) return;

    p.save();
    p.setClipping(false);
    // Two passes so the white divider reads on ANY background (the browser's drop shadow); the
    // casing is 1 px wider each side.
    const markMetrics::Table& m = markMetrics::table();
    QPen casing(QColor(0, 0, 0, 150), m.dividerLineWidthPx + 2.0);
    casing.setCapStyle(Qt::RoundCap);
    QPen white(QColor(255, 255, 255, 240), m.dividerLineWidthPx);
    white.setCapStyle(Qt::RoundCap);
    const double r = m.dividerKnobRadiusPx;
    if (mode == CompareMode::VERTICAL) {
      const double x = w * f;
      p.setPen(casing);
      p.drawLine(QPointF(x, 0), QPointF(x, h));
      p.setPen(white);
      p.drawLine(QPointF(x, 0), QPointF(x, h));
      p.setPen(QPen(QColor(0, 0, 0, 150), 1.5));
      p.setBrush(QColor(255, 255, 255, 240));
      p.drawEllipse(QPointF(x, h / 2), r, r);
    } else {
      const double y = h * f;
      p.setPen(casing);
      p.drawLine(QPointF(0, y), QPointF(w, y));
      p.setPen(white);
      p.drawLine(QPointF(0, y), QPointF(w, y));
      p.setPen(QPen(QColor(0, 0, 0, 150), 1.5));
      p.setBrush(QColor(255, 255, 255, 240));
      p.drawEllipse(QPointF(w / 2, y), r, r);
    }
    p.restore();
  }

}  // namespace stencil::gui
