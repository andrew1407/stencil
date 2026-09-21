#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "iconSet.hpp"
#include "LogoHoverFx.hpp"
#include "modalReveal.hpp"
#include "numericInput.hpp"
#include "SearchCombo.hpp"
#include "ControlsPill.hpp"
#include "OpenImageButton.hpp"
#include "theme.hpp"
#include "../../support/control/controlReveal.hpp"   // section buttons come and go as sand
#include "../../support/icon/iconMotion.hpp"
#include "../../support/motion/ShimmerOverlay.hpp"
#include "../../support/control/WrapRow.hpp"     // rows wrap like the browser's, never overflow into "»"

#include <QAbstractSpinBox>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

// MainWindow's toolbar assembly: the Line · Point sections and the style wiring.

namespace stencil::gui {

  void MainWindow::buildStyleToolbar() {
    QToolBar* row = toolRow();
    styleToolbar = row;

    // toolbar.js:40 #lineColor
    lineColorBtn = new QToolButton(this);
    lineColorBtn->setToolTip("Line color");
    updateColorSwatch(lineColorBtn, lineColorValue);

    // toolbar.js #point-color. Empty means inherit (core pointColorOr), so the swatch shows the
    // effective colour.
    pointColorBtn = new QToolButton(this);
    pointColorBtn->setToolTip("Point color — new lines");
    updateColorSwatch(pointColorBtn, effectiveDefaultPointColor());

    // toolbar.js:41-42, min/max mirrored; each keeps an inline caption like the browser's LINE /
    // POINT clusters.
    lineThickness = new ExprSpinBox(this);
    lineThickness->setRange(1, 20);
    lineThickness->setValue(2);
    lineThickness->setToolTip("Line thickness");
    lineThickness->setMaximumWidth(56);
    lineThickness->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    pointSize = new ExprSpinBox(this);
    pointSize->setRange(1, 30);
    pointSize->setValue(4);
    pointSize->setToolTip("Point size");
    pointSize->setMaximumWidth(56);
    pointSize->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // toolbar.js:43-47; data carries the canonical value.
    lineStyle = new SearchComboBox(this, /*searchable=*/false);
    lineStyle->addItem("Solid", "solid");
    lineStyle->addItem("Dashed", "dashed");
    lineStyle->addItem("Dotted", "dotted");
    lineStyle->setToolTip("Line style");

    addWrappedSeparator(row);
    addWrapped(row, makeToolSection("Line", {}, {
        new QLabel(" Color ", this), lineColorBtn,
        new QLabel(" Thickness ", this), lineThickness, lineStyle }));
    addWrappedSeparator(row);
    addWrapped(row, makeToolSection("Point", {}, {
        new QLabel(" Color ", this), pointColorBtn,
        new QLabel(" Size ", this), pointSize }));
    // No trailing separator (browser: toolbar.js syncWrappedSeparators hides exactly that one).

    // Flip the canvas mode only; the drawModeChanged handler echoes back the label/tooltip.
    connect(drawModeBtn, &QToolButton::clicked, this, [this] {
      const auto next = canvas->getDrawMode() == CanvasWidget::DrawMode::RECT
                            ? CanvasWidget::DrawMode::LINE
                            : CanvasWidget::DrawMode::RECT;
      canvas->setDrawMode(next);
      persistSettings();
    });
    // drawingApp.js syncDrawModeUI ~1125.
    connect(canvas, &CanvasWidget::drawModeChanged, this,
            [this](CanvasWidget::DrawMode mode) {
              // Glyph + word cross over together (support/faceSwap.hpp), like Start/Stop.
              syncDrawModeFace(mode == CanvasWidget::DrawMode::RECT, true);
            });

    // drawingApp.js:155
    connect(lineColorBtn, &QToolButton::clicked, this, [this] {
      const QColor c =
          support::pickColorAnimated(lineColorValue, this, "Line color", lineColorBtn);
      if (!c.isValid()) return;
      lineColorValue = c;
      updateColorSwatch(lineColorBtn, c);
      settings.defaultColor = c.name(QColor::HexRgb);
      if (pointColorBtn && settings.defaultPointColor.isEmpty())
        updateColorSwatch(pointColorBtn, c);
      onLineStyleControlChanged();
    });
    // Picking the line colour again stores empty (inherit), so later line-colour changes keep
    // carrying the points.
    connect(pointColorBtn, &QToolButton::clicked, this, [this] {
      const QColor c = support::pickColorAnimated(effectiveDefaultPointColor(), this,
                                                  "Point color", pointColorBtn);
      if (!c.isValid()) return;
      settings.defaultPointColor =
          (c.rgb() == lineColorValue.rgb()) ? QString() : c.name(QColor::HexRgb);
      updateColorSwatch(pointColorBtn, effectiveDefaultPointColor());
      onLineStyleControlChanged();
    });
    // drawingApp.js:156-178
    connect(lineThickness, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) {
              settings.defaultThickness = v;
              if (thickSpin) {  // keep the context-menu spinbox in sync (two-way)
                QSignalBlocker b(thickSpin);
                thickSpin->setValue(v);
              }
              onLineStyleControlChanged();
            });
    connect(pointSize, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) {
              settings.defaultPointSize = v;
              if (pointSpin) {  // keep the context-menu spinbox in sync (two-way)
                QSignalBlocker b(pointSpin);
                pointSpin->setValue(v);
              }
              onLineStyleControlChanged();
            });
    connect(lineStyle, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
              applyLineStyle(lineStyle->currentData().toString());
            });

    // drawingApp.js:228-238; shared with the context menu.
    connect(imageFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
              applyImageFilter(imageFilter->currentData().toString());
            });
    // Hover-preview repaints only; leaving without a pick reverts (searchCombo setPreview, browser
    // twin).
    static_cast<SearchComboBox*>(imageFilter)->setPreview([this](const QString& mode) {
      canvas->setImageFilter(mode, filterColorValue);
    });
    // drawingApp.js:240-249
    connect(filterColorBtn, &QToolButton::clicked, this, [this] {
      const QColor c =
          support::pickColorAnimated(filterColorValue, this, "Tint color", filterColorBtn);
      if (c.isValid()) applyTintColor(c);
    });
  }
}  // namespace stencil::gui

