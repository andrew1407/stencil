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

    addWell(r, color, colorHex, "Default color for newly drawn lines — click to change",
         "Default line color");
    addRow(r, tr("Line color"), color);

    thickness = new QDoubleSpinBox(r.host);
    thickness->setRange(1, 20);  // LIMITS.thickMin/thickMax
    thickness->setDecimals(0);   // the browser field is an integer
    thickness->setValue(current.defaultThickness);
    thickness->setToolTip("Default stroke thickness for new lines (px)");
    addRow(r, tr("Line thickness"), thickness);
    connect(thickness, &QAbstractSpinBox::editingFinished, this, [this] { applyLive(); });

    pointSize = new QDoubleSpinBox(r.host);
    pointSize->setRange(1, 30);  // LIMITS.pointMin/pointMax
    pointSize->setDecimals(0);
    pointSize->setValue(current.defaultPointSize);
    pointSize->setToolTip("Default point size for new lines (px)");
    addRow(r, tr("Point size"), pointSize);
    connect(pointSize, &QAbstractSpinBox::editingFinished, this, [this] { applyLive(); });

    style = addCombo(r, "Default stroke style for new lines");
    style->addItem("Solid", "solid");
    style->addItem("Dashed", "dashed");
    style->addItem("Dotted", "dotted");
    {
      const int idx = style->findData(current.defaultStyle);
      style->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    addRow(r, tr("Line style"), style);
    connect(style, &QComboBox::activated, this, [this] { applyLive(); });

    addWell(r, fillColor, fillHex, "Fill applied to newly locked areas — click to change",
         "Area fill color");
    addRow(r, tr("Area fill (new locked areas)"), fillColor);

    addSection(r, tr("Drawing behavior"));

    holdDelay = new QSpinBox(r.host);
    holdDelay->setRange(100, 3000);  // clamp mirrors CanvasWidget::setHoldDrawDelay
    holdDelay->setSingleStep(50);
    holdDelay->setValue(current.holdDrawDelay);
    holdDelay->setToolTip("Press-and-hold delay before hold-to-draw places a point");
    addRow(r, tr("Hold-to-draw delay (ms)"), holdDelay);
    connect(holdDelay, &QAbstractSpinBox::editingFinished, this, [this] { applyLive(); });

    addSection(r, tr("Highlight styles"));

    addWell(r, selGlow, selGlowHex, "Selected line/point glow — click to change",
         "Selection glow color");
    addRow(r, tr("Selected line/point glow"), selGlow);
    addWell(r, hoverRing, hoverRingHex, "Point hover ring — click to change",
         "Point hover ring color");
    addRow(r, tr("Point hover ring"), hoverRing);
    addWell(r, focusRing, focusRingHex, "Focused/clicked point ring — click to change",
         "Point focus ring color");
    addRow(r, tr("Point focus ring"), focusRing);
  }

}
