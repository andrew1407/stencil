#pragma once
#include "cropGeometry.hpp"
#include <QDialog>
#include <QImage>
#include <QWidget>

class QHBoxLayout;
class QPushButton;

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
    CropPreview(const QImage& original, double pageWidthCm, double pageHeightCm,
                const core::CropRect& initial, QWidget* parent = nullptr);

    core::CropRect cropRect() const { return rect_; }
    bool album() const { return album_; }
    void setAlbum(bool album);  // flips orientation, re-centers the crop
    void setFitBox(const QSize& box);   // re-fit the display scale into a new box

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

    QImage original_;
    double pageWidthCm_;
    double pageHeightCm_;
    double aspect_;
    bool album_;
    core::CropRect rect_;  // original-image pixels
    double scale_ = 1.0;   // display px per image px
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
