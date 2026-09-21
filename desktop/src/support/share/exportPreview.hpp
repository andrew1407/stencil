#pragma once

#include <QPoint>
#include <QRect>

class QImage;
class QWidget;

// Floating thumbnail shown while Alt is held over an export variant row — port of
// browser js/ui/preview.js. One window shared by every export menu.
namespace stencil::support {

  // `ownerRect` is the row in the menu's own coordinates (QMenu::actionGeometry), the dust's origin
  // (browser js/ui/motion.js surfaceIn); `dustFromGlobal` overrides it for a KEY-triggered appearance.
  void showExportPreview(const QImage& image, QWidget* owner = nullptr,
                         const QRect& ownerRect = QRect(),
                         const QPoint& dustFromGlobal = QPoint());

  // `dustToGlobal` aims the leave elsewhere than the row (Alt released = into the cursor).
  void hideExportPreview(const QPoint& dustToGlobal = QPoint());

  // nullptr while hidden; lets the menu's mouse tracking hide the preview off-row
  // (browser wireAltPreview mouseleave parity).
  QWidget* exportPreviewOwner();
  QRect exportPreviewOwnerRect();

}  // namespace stencil::support
