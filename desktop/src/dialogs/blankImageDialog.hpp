#pragma once
#include <QColor>
#include <QDialog>

class QRadioButton;
class QSpinBox;
class QToolButton;

// Blank-image creator. Mirrors browser/js/ui/blankImageModal.js: pick a fill
// color (white / black preset or a custom picked color) and a pixel size that
// defaults to the current page rendered at 96 dpi (core::defaultBlankSizePx).
// exec(), then read color()/widthPx()/heightPx() to generate the image.
namespace stencil::gui {

  class BlankImageDialog : public QDialog {
    Q_OBJECT
   public:
    explicit BlankImageDialog(int defaultWidthPx, int defaultHeightPx,
                              QWidget* parent = nullptr);

    QColor color() const;
    int widthPx() const;
    int heightPx() const;

   private:
    void pickCustomColor();

    QRadioButton* white_ = nullptr;
    QRadioButton* black_ = nullptr;
    QRadioButton* custom_ = nullptr;
    QToolButton* customSwatch_ = nullptr;
    QSpinBox* width_ = nullptr;
    QSpinBox* height_ = nullptr;
    QColor customColor_{Qt::white};
  };

}
