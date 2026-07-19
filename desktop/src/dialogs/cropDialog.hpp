#pragma once
#include "cropGeometry.hpp"
#include <QDialog>
#include <QImage>
#include <QWidget>

class QPushButton;

// Image-crop dialog. Mirrors browser/js/ui/cropModal.js: shows the full ORIGINAL
// image with an overlaid crop rectangle locked to the page aspect ratio (A3/A4 =
// √2, or the custom W×H). The rectangle can be moved and resized from its four
// corners, with an Album/Portrait toggle; exec(), then read cropRect() (in
// original-image pixels). The geometry math is the shared C++ core (cropGeometry).
namespace stencil::gui {

  // Interactive preview: paints the scaled image + crop overlay and handles the
  // move / corner-resize gestures. All rect math is in original-image pixels.
  class CropPreview : public QWidget {
    Q_OBJECT
   public:
    CropPreview(const QImage& original, double pageWidthCm, double pageHeightCm,
                const core::CropRect& initial, QWidget* parent = nullptr);

    core::CropRect cropRect() const { return rect_; }
    bool album() const { return album_; }
    void setAlbum(bool album);  // flips orientation, re-centers the crop

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

    QImage original_;
    double pageWidthCm_;
    double pageHeightCm_;
    double aspect_;
    bool album_;
    core::CropRect rect_;  // original-image pixels
    double scale_ = 1.0;   // display px per image px
    int iw_ = 0, ih_ = 0;

    // Active gesture.
    enum class Drag { None, Move, Resize };
    Drag drag_ = Drag::None;
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
    CropPreview* preview_ = nullptr;
    QPushButton* orientationBtn_ = nullptr;
  };

}
