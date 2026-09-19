// The drawing sections: the defaults new lines take, hold-to-draw, and the highlight rings.
#include "SettingsDialog.hpp"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSpinBox>

namespace stencil::gui {

  void SettingsDialog::buildDrawingRows(Rows& r, const Settings& current) {
    // Drawing defaults (applied to new lines)
    addSection(r, tr("Drawing defaults (applied to new lines)"));

    addWell(r, color_, colorHex_, "Default color for newly drawn lines — click to change",
         "Default line color");
    addRow(r, tr("Line color"), color_);

    thickness_ = new QDoubleSpinBox(r.host);
    thickness_->setRange(1, 20);  // LIMITS.thickMin/thickMax
    thickness_->setDecimals(0);   // the browser field is an integer
    thickness_->setValue(current.defaultThickness);
    thickness_->setToolTip("Default stroke thickness for new lines (px)");
    addRow(r, tr("Line thickness"), thickness_);
    connect(thickness_, &QAbstractSpinBox::editingFinished, this, [this] { applyLive(); });

    pointSize_ = new QDoubleSpinBox(r.host);
    pointSize_->setRange(1, 30);  // LIMITS.pointMin/pointMax
    pointSize_->setDecimals(0);
    pointSize_->setValue(current.defaultPointSize);
    pointSize_->setToolTip("Default point size for new lines (px)");
    addRow(r, tr("Point size"), pointSize_);
    connect(pointSize_, &QAbstractSpinBox::editingFinished, this, [this] { applyLive(); });

    style_ = addCombo(r, "Default stroke style for new lines");
    style_->addItem("Solid", "solid");
    style_->addItem("Dashed", "dashed");
    style_->addItem("Dotted", "dotted");
    {
      const int idx = style_->findData(current.defaultStyle);
      style_->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    addRow(r, tr("Line style"), style_);
    connect(style_, &QComboBox::activated, this, [this] { applyLive(); });

    addWell(r, fillColor_, fillHex_, "Fill applied to newly locked areas — click to change",
         "Area fill color");
    addRow(r, tr("Area fill (new locked areas)"), fillColor_);

    addSection(r, tr("Drawing behavior"));

    holdDelay_ = new QSpinBox(r.host);
    holdDelay_->setRange(100, 3000);  // clamp mirrors CanvasWidget::setHoldDrawDelay
    holdDelay_->setSingleStep(50);
    holdDelay_->setValue(current.holdDrawDelay);
    holdDelay_->setToolTip("Press-and-hold delay before hold-to-draw places a point");
    addRow(r, tr("Hold-to-draw delay (ms)"), holdDelay_);
    connect(holdDelay_, &QAbstractSpinBox::editingFinished, this, [this] { applyLive(); });

    addSection(r, tr("Highlight styles"));

    addWell(r, selGlow_, selGlowHex_, "Selected line/point glow — click to change",
         "Selection glow color");
    addRow(r, tr("Selected line/point glow"), selGlow_);
    addWell(r, hoverRing_, hoverRingHex_, "Point hover ring — click to change",
         "Point hover ring color");
    addRow(r, tr("Point hover ring"), hoverRing_);
    addWell(r, focusRing_, focusRingHex_, "Focused/clicked point ring — click to change",
         "Point focus ring color");
    addRow(r, tr("Point focus ring"), focusRing_);
  }

}
