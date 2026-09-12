#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "canvasWidget.hpp"
#include "iconSet.hpp"
#include "logoHoverFx.hpp"
#include "modalReveal.hpp"
#include "numericInput.hpp"
#include "searchCombo.hpp"
#include "controlsPill.hpp"
#include "openImageButton.hpp"
#include "theme.hpp"
#include "../support/controlReveal.hpp"   // section buttons come and go as sand
#include "../support/iconMotion.hpp"
#include "../support/shimmerOverlay.hpp"
#include "../support/wrapRow.hpp"     // rows wrap like the browser's, never overflow into "»"

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
    styleToolbar_ = row;

    // toolbar.js:40 #lineColor
    lineColorBtn_ = new QToolButton(this);
    lineColorBtn_->setToolTip("Line color");
    updateColorSwatch(lineColorBtn_, lineColorValue_);

    // toolbar.js #point-color. Empty means inherit (core pointColorOr), so the swatch shows the
    // effective colour.
    pointColorBtn_ = new QToolButton(this);
    pointColorBtn_->setToolTip("Point color — new lines");
    updateColorSwatch(pointColorBtn_, effectiveDefaultPointColor());

    // toolbar.js:41-42, min/max mirrored; each keeps an inline caption like the browser's LINE /
    // POINT clusters.
    lineThickness_ = new ExprSpinBox(this);
    lineThickness_->setRange(1, 20);
    lineThickness_->setValue(2);
    lineThickness_->setToolTip("Line thickness");
    lineThickness_->setMaximumWidth(56);
    lineThickness_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    pointSize_ = new ExprSpinBox(this);
    pointSize_->setRange(1, 30);
    pointSize_->setValue(4);
    pointSize_->setToolTip("Point size");
    pointSize_->setMaximumWidth(56);
    pointSize_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // toolbar.js:43-47; data carries the canonical value.
    lineStyle_ = new SearchComboBox(this, /*searchable=*/false);
    lineStyle_->addItem("Solid", "solid");
    lineStyle_->addItem("Dashed", "dashed");
    lineStyle_->addItem("Dotted", "dotted");
    lineStyle_->setToolTip("Line style");

    addWrappedSeparator(row);
    addWrapped(row, makeToolSection("Line", {}, {
        new QLabel(" Color ", this), lineColorBtn_,
        new QLabel(" Thickness ", this), lineThickness_, lineStyle_ }));
    addWrappedSeparator(row);
    addWrapped(row, makeToolSection("Point", {}, {
        new QLabel(" Color ", this), pointColorBtn_,
        new QLabel(" Size ", this), pointSize_ }));
    // No trailing separator (browser: toolbar.js syncWrappedSeparators hides exactly that one).

    // Flip the canvas mode only; the drawModeChanged handler echoes back the label/tooltip.
    connect(drawModeBtn_, &QToolButton::clicked, this, [this] {
      const auto next = canvas_->drawMode() == CanvasWidget::DrawMode::Rect
                            ? CanvasWidget::DrawMode::Line
                            : CanvasWidget::DrawMode::Rect;
      canvas_->setDrawMode(next);
      persistSettings();
    });
    // drawingApp.js syncDrawModeUI ~1125.
    connect(canvas_, &CanvasWidget::drawModeChanged, this,
            [this](CanvasWidget::DrawMode mode) {
              // Glyph + word cross over together (support/faceSwap.hpp), like Start/Stop.
              syncDrawModeFace(mode == CanvasWidget::DrawMode::Rect, true);
            });

    // drawingApp.js:155
    connect(lineColorBtn_, &QToolButton::clicked, this, [this] {
      const QColor c =
          support::pickColorAnimated(lineColorValue_, this, "Line color", lineColorBtn_);
      if (!c.isValid()) return;
      lineColorValue_ = c;
      updateColorSwatch(lineColorBtn_, c);
      settings_.defaultColor = c.name(QColor::HexRgb);
      if (pointColorBtn_ && settings_.defaultPointColor.isEmpty())
        updateColorSwatch(pointColorBtn_, c);
      onLineStyleControlChanged();
    });
    // Picking the line colour again stores empty (inherit), so later line-colour changes keep
    // carrying the points.
    connect(pointColorBtn_, &QToolButton::clicked, this, [this] {
      const QColor c = support::pickColorAnimated(effectiveDefaultPointColor(), this,
                                                  "Point color", pointColorBtn_);
      if (!c.isValid()) return;
      settings_.defaultPointColor =
          (c.rgb() == lineColorValue_.rgb()) ? QString() : c.name(QColor::HexRgb);
      updateColorSwatch(pointColorBtn_, effectiveDefaultPointColor());
      onLineStyleControlChanged();
    });
    // drawingApp.js:156-178
    connect(lineThickness_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) {
              settings_.defaultThickness = v;
              if (thickSpin_) {  // keep the context-menu spinbox in sync (two-way)
                QSignalBlocker b(thickSpin_);
                thickSpin_->setValue(v);
              }
              onLineStyleControlChanged();
            });
    connect(pointSize_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) {
              settings_.defaultPointSize = v;
              if (pointSpin_) {  // keep the context-menu spinbox in sync (two-way)
                QSignalBlocker b(pointSpin_);
                pointSpin_->setValue(v);
              }
              onLineStyleControlChanged();
            });
    connect(lineStyle_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
              applyLineStyle(lineStyle_->currentData().toString());
            });

    // drawingApp.js:228-238; shared with the context menu.
    connect(imageFilter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
              applyImageFilter(imageFilter_->currentData().toString());
            });
    // Hover-preview repaints only; leaving without a pick reverts (searchCombo setPreview, browser
    // twin).
    static_cast<SearchComboBox*>(imageFilter_)->setPreview([this](const QString& mode) {
      canvas_->setImageFilter(mode, filterColorValue_);
    });
    // drawingApp.js:240-249
    connect(filterColorBtn_, &QToolButton::clicked, this, [this] {
      const QColor c =
          support::pickColorAnimated(filterColorValue_, this, "Tint color", filterColorBtn_);
      if (c.isValid()) applyTintColor(c);
    });
  }
}  // namespace stencil::gui

