#include "blankImageDialog.hpp"
#include "guiHelpers.hpp"
#include <QColorDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace stencil::gui {

  BlankImageDialog::BlankImageDialog(int defaultWidthPx, int defaultHeightPx,
                                     QWidget* parent)
      : QDialog(parent) {
    setWindowTitle("New Blank Image");
    setMinimumWidth(320);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;

    // Fill color: white/black presets + a custom picked color, mirroring the
    // browser modal's preset chips + <input type=color>.
    auto* colorRow = new QHBoxLayout;
    white_ = new QRadioButton("White", this);
    white_->setToolTip("Fill the blank image with white");
    black_ = new QRadioButton("Black", this);
    black_->setToolTip("Fill the blank image with black");
    custom_ = new QRadioButton("Custom:", this);
    custom_->setToolTip("Fill the blank image with the picked custom color");
    white_->setChecked(true);
    customSwatch_ = new QToolButton(this);
    customSwatch_->setToolTip("Pick a custom fill color");
    setColorSwatch(customSwatch_, customColor_);
    connect(customSwatch_, &QToolButton::clicked, this,
            &BlankImageDialog::pickCustomColor);
    colorRow->addWidget(white_);
    colorRow->addWidget(black_);
    colorRow->addWidget(custom_);
    colorRow->addWidget(customSwatch_);
    colorRow->addStretch(1);
    form->addRow("Fill color:", colorRow);

    // Pixel size, seeded by the caller with the page rendered at 96 dpi
    // (core::defaultBlankSizePx). Bounds mirror the browser inputs (1–8192).
    width_ = new QSpinBox(this);
    width_->setRange(1, 8192);
    width_->setSuffix(" px");
    width_->setValue(defaultWidthPx);
    width_->setToolTip("Blank image width in pixels (1–8192)");
    height_ = new QSpinBox(this);
    height_->setRange(1, 8192);
    height_->setSuffix(" px");
    height_->setValue(defaultHeightPx);
    height_->setToolTip("Blank image height in pixels (1–8192)");
    form->addRow("Width:", width_);
    form->addRow("Height:", height_);
    layout->addLayout(form);

    auto* hint = new QLabel("Size defaults match the current page size.", this);
    hint->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(hint);

    auto* box = makeButtonBox(this, QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    box->button(QDialogButtonBox::Ok)->setText("Create");
    layout->addWidget(box);
  }

  void BlankImageDialog::pickCustomColor() {
    const QColor c = QColorDialog::getColor(customColor_, this, "Fill color",
                                            QColorDialog::DontUseNativeDialog);
    if (!c.isValid()) return;
    customColor_ = c;
    custom_->setChecked(true);
    setColorSwatch(customSwatch_, customColor_);
  }

  QColor BlankImageDialog::color() const {
    if (white_->isChecked()) return QColor(Qt::white);
    if (black_->isChecked()) return QColor(Qt::black);
    return customColor_;
  }

  int BlankImageDialog::widthPx() const { return width_->value(); }
  int BlankImageDialog::heightPx() const { return height_->value(); }

}
