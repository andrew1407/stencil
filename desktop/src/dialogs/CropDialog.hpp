#pragma once
#include "cropGeometry.hpp"
#include <QDialog>
#include <QImage>
#include <QWidget>

class QHBoxLayout;
class QPushButton;
class QVariantAnimation;

// Image-crop dialog (browser/js/ui/cropModal.js): the full ORIGINAL image under a crop
// rectangle locked to the page aspect, movable and corner-resizable with an
// Album/Portrait toggle. exec(), then read cropRect() in original-image pixels. The
// geometry is the shared core (cropGeometry); the chrome is support/modalChrome.
namespace stencil::gui {

  struct ModalChrome;

  // Interactive preview: paints the scaled image + crop overlay and handles the
  // move / corner-resize gestures. All rect math is in original-image pixels.
  class CropPreview : public QWidget {
    Q_OBJECT
   public:
    // `autoFitScreen`: the constructor's own screen-relative first fit (previewFitBox) — on for the
    // standalone crop editor, which relies on it; off for a caller that fits its own small box right
    // after (OpenImageDialog's inline stage), so that box is the only size this widget is asked to be.
    CropPreview(const QImage& original, double pageWidthCm, double pageHeightCm,
                const core::CropRect& initial, QWidget* parent = nullptr,
                bool autoFitScreen = true);

    core::CropRect cropRect() const { return rect_; }
    bool album() const { return album_; }
    void setAlbum(bool album);  // flips orientation, re-centers the crop
    // A DIFFERENT page picked (not a flip): no reciprocal aspect to carry the old box
    // across, so this is a fresh default at the new aspect — same as a first Crop tick.
    void setPageSize(double pageWidthCm, double pageHeightCm);
    void setFitBox(const QSize& box);   // re-fit the display scale into a new box
    // Swap the pixels under the box — a scrubbed video frame. The rect survives while
    // the dimensions do (every frame of one video shares them); otherwise it re-centres.
    void setOriginal(const QImage& original);
    // Where the scaled picture is actually painted — what a bar under it is sized to.
    QRect paintedRect() const { return imageRect(); }

   signals:
    void cropChanged();  // rect or orientation changed (updates the dialog label)

   protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    // Mouse wheel + trackpad pinch (native zoom gesture) grow/shrink the crop from its centre.
    void wheelEvent(QWheelEvent* event) override;
    bool event(QEvent* event) override;

   private:
    core::Point toImage(const QPoint& widgetPos) const;  // display px -> image px
    int cornerAt(const QPoint& widgetPos) const;         // handle hit-test (-1 none)
    QRectF displayRect() const;  // crop rect in display (widget) coordinates
    QRect imageRect() const;     // where the scaled image is painted (inset for handles)
    void flyRectFrom(const core::CropRect& from);   // the painted box eases there → rect_
    void settleRect();                               // …or lands on rect_ at once

    QImage original_;
    double pageWidthCm_;
    double pageHeightCm_;
    double aspect_;
    bool album_;
    core::CropRect rect_;  // original-image pixels — always the real one
    QVariantAnimation* rectAnim_ = nullptr;   // a flip's flight; paints shownRect_ meanwhile
    core::CropRect shownRect_;
    bool flying_ = false;
    double scale_ = 1.0;   // display px per image px
    QSize fitBox_;          // the box last asked for, so a size CHANGE re-fits into it too
    int iw_ = 0, ih_ = 0;

    // Active gesture.
    enum class Drag { NONE, MOVE, RESIZE };
    Drag drag_ = Drag::NONE;
    int dragCorner_ = -1;
    core::Point dragStartImg_;
    core::CropRect dragStartRect_;
  };

  class CropDialog : public QDialog {
    Q_OBJECT
   public:
    CropDialog(const QImage& original, double pageWidthCm, double pageHeightCm,
               bool album, const core::CropRect& initial, QWidget* parent = nullptr);

    core::CropRect cropRect() const;

   private:
    void fitToScreen(const ModalChrome& chrome, const QHBoxLayout* footer);

    CropPreview* preview_ = nullptr;
    QPushButton* orientationBtn_ = nullptr;
  };

}
