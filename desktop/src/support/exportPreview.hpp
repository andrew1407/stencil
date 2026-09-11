#pragma once

#include <QPoint>
#include <QRect>

class QImage;
class QWidget;

// A small floating thumbnail near the cursor, shown while Alt is held over a
// copy/download-image variant menu row — desktop port of the browser's
// js/ui/exportPreview.js. The canvas context menu's nested Copy/Download Image
// submenus and the toolbar's export-options popups share this one window.
namespace stencil::support {

  // Show (or move/update) the preview near the current cursor position. `owner`
  // is the menu the row lives in and `ownerRect` that row's rect in the menu's own
  // coordinates (QMenu::actionGeometry) — together they give the preview's dust
  // flight (browser parity: js/ui/motion.js surfaceIn) something to stream out of.
  // Omitted, the preview still shows, just without the flight. `dustFromGlobal`
  // overrides where the gather streams out of (a KEY-triggered appearance — Alt
  // pressed — forms from the cursor; a pointer-driven one keeps the row's centre).
  void showExportPreview(const QImage& image, QWidget* owner = nullptr,
                         const QRect& ownerRect = QRect(),
                         const QPoint& dustFromGlobal = QPoint());

  // Hide it — called when Alt releases, the hovered row changes to nothing, or
  // the owning menu closes. Comes apart into the row it was last shown for —
  // unless `dustToGlobal` aims it elsewhere (Alt released = back into the cursor).
  void hideExportPreview(const QPoint& dustToGlobal = QPoint());

  // The menu the visible preview belongs to (nullptr while hidden) and its row's
  // rect — lets the menu's own mouse tracking hide the preview the moment the
  // pointer glides off that row (browser wireAltPreview mouseleave parity).
  QWidget* exportPreviewOwner();
  QRect exportPreviewOwnerRect();

}  // namespace stencil::support
