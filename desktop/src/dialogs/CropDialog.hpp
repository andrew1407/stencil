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
  // move / corner-resize gestures. All cropBox math is in original-image pixels.
  class CropPreview : public QWidget {
    Q_OBJECT
   public:
    // `autoFitScreen`: the constructor's own screen-relative first fit (previewFitBox) - on for the
    // standalone crop editor, off for a caller that fits its own small box right after.
    CropPreview(const QImage& original, double pageWidthCm, double pageHeightCm,
                const core::CropRect& initial, QWidget* parent = nullptr,
                bool autoFitScreen = true);

    core::CropRect cropRect() const { return cropBox; }
    bool getAlbum() const { return album; }
    void setAlbum(bool album);  // flips orientation, re-centers the crop
    // A DIFFERENT page picked (not a flip): no reciprocal aspect to carry the old box
    // across, so this is a fresh default at the new aspect — same as a first Crop tick.
    void setPageSize(double pageWidthCm, double pageHeightCm);
    void setFitBox(const QSize& box);   // re-fit the display scale into a new box
    // Swap the pixels under the box — a scrubbed video frame. The cropBox survives while
    // the dimensions do (every frame of one video shares them); otherwise it re-centres.
    void setOriginal(const QImage& original);
    // Where the scaled picture is actually painted — what a bar under it is sized to.
    QRect paintedRect() const { return imageRect(); }

   signals:
    void cropChanged();  // cropBox or orientation changed (updates the dialog label)

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
    QRectF displayRect() const;  // crop cropBox in display (widget) coordinates
    QRect imageRect() const;     // where the scaled image is painted (inset for handles)
    void flyRectFrom(const core::CropRect& from);   // the painted box eases there → cropBox
    void settleRect();                               // …or lands on cropBox at once

    QImage original;
    double pageWidthCm;
    double pageHeightCm;
    double aspect;
    bool album;
    core::CropRect cropBox;  // original-image pixels — always the real one
    QVariantAnimation* rectAnim = nullptr;   // a flip's flight; paints shownRect meanwhile
    core::CropRect shownRect;
    bool flying = false;
    double scale = 1.0;   // display px per image px
    QSize fitBox;          // the box last asked for, so a size CHANGE re-fits into it too
    int iw = 0, ih = 0;

    // Active gesture.
    enum class Drag { NONE, MOVE, RESIZE };
    Drag drag = Drag::NONE;
    int dragCorner = -1;
    core::Point dragStartImg;
    core::CropRect dragStartRect;
  };

  class CropDialog : public QDialog {
    Q_OBJECT
   public:
    CropDialog(const QImage& original, double pageWidthCm, double pageHeightCm,
               bool album, const core::CropRect& initial, QWidget* parent = nullptr);

    core::CropRect cropRect() const;

   private:
    void fitToScreen(const ModalChrome& chrome, const QHBoxLayout* footer);

    CropPreview* preview = nullptr;
    QPushButton* orientationBtn = nullptr;
  };

}
