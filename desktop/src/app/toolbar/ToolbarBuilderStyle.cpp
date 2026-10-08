#include "MainWindow.hpp"
#include "ToolbarBuilder.hpp"
#include "CanvasWidget.hpp"
#include "numericInput.hpp"
#include "SearchCombo.hpp"
#include "../../support/control/WrapRow.hpp"     // rows wrap like the browser's, never overflow into "»"
#include "../../support/control/lineLimits.hpp"
#include "../../support/control/dblReset.hpp"
#include "colorDrag.hpp"
#include "defaultVisuals.hpp"
#include "modalReveal.hpp"

#include <QLabel>
#include <QToolButton>

// MainWindow's toolbar assembly: the Line · Point sections and the style wiring.

namespace stencil::gui {

  void ToolbarBuilder::buildStyleToolbar() {
    QToolBar* row = toolRow();
    w.tools.styleToolbar = row;

    // toolbar.js:40 #lineColor
    w.tools.lineColorBtn = new QToolButton(&w);
    w.tools.lineColorBtn->setToolTip("Line color");
    w.updateColorSwatch(w.tools.lineColorBtn, w.tools.lineColorValue);

    // toolbar.js #point-color. Empty means inherit (core pointColorOr), so the swatch shows the
    // effective colour.
    w.tools.pointColorBtn = new QToolButton(&w);
    w.tools.pointColorBtn->setToolTip("Point color");
    w.updateColorSwatch(w.tools.pointColorBtn, w.parts.styleControls.effectiveDefaultPointColor());

    // toolbar.js:41-42, min/max from LIMITS; each keeps an inline caption like the browser's LINE /
    // POINT clusters.
    const support::lineLimits::Table& limits = support::lineLimits::table();
    w.tools.lineThickness = new ExprSpinBox(&w);
    w.tools.lineThickness->setRange(limits.thickMin, limits.thickMax);
    w.tools.lineThickness->setValue(2);
    w.tools.lineThickness->setToolTip("Line thickness");
    w.tools.lineThickness->setMaximumWidth(56);
    w.tools.lineThickness->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    w.tools.pointSize = new ExprSpinBox(&w);
    w.tools.pointSize->setRange(limits.pointMin, limits.pointMax);
    w.tools.pointSize->setValue(4);
    w.tools.pointSize->setToolTip("Point size");
    w.tools.pointSize->setMaximumWidth(56);
    w.tools.pointSize->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // toolbar.js:43-47; data carries the canonical value.
    w.tools.lineStyle = new SearchComboBox(&w, /*searchable=*/false);
    w.tools.lineStyle->addItem("Solid", "solid");
    w.tools.lineStyle->addItem("Dashed", "dashed");
    w.tools.lineStyle->addItem("Dotted", "dotted");
    w.tools.lineStyle->setToolTip("Line style");

    addWrappedSeparator(row);
    addWrapped(row, makeToolSection("Line", {}, {
        new QLabel(" Color ", &w), w.tools.lineColorBtn,
        new QLabel(" Thickness ", &w), w.tools.lineThickness, w.tools.lineStyle }));
    addWrappedSeparator(row);
    addWrapped(row, makeToolSection("Point", {}, {
        new QLabel(" Color ", &w), w.tools.pointColorBtn,
        new QLabel(" Size ", &w), w.tools.pointSize }));
    // No trailing separator (browser: toolbar.js syncWrappedSeparators hides exactly that one).

    // Flip the canvas mode only; the drawModeChanged handler echoes back the label/tooltip.
    QObject::connect(w.tools.drawModeBtn, &QToolButton::clicked, &w, [this] {
      const auto next = w.canvas->getDrawMode() == CanvasWidget::DrawMode::RECT
                            ? CanvasWidget::DrawMode::LINE
                            : CanvasWidget::DrawMode::RECT;
      w.canvas->setDrawMode(next);
      w.persistSettings();
    });
    // drawingApp.js syncDrawModeUI ~1125.
    QObject::connect(w.canvas, &CanvasWidget::drawModeChanged, &w,
                     [this](CanvasWidget::DrawMode mode) {
                       // Glyph + word cross over together (support/theme/faceSwap.hpp), like Start/Stop.
                       w.parts.theme.syncDrawModeFace(mode == CanvasWidget::DrawMode::RECT, true);
                     });

    // drawingApp.js:155; a double-click puts the canonical default back (browser dblReset.js 'line-color').
    const auto applyLineColor = [this](const QColor& c) {
      w.tools.lineColorValue = c;
      w.updateColorSwatch(w.tools.lineColorBtn, c);
      w.settings.defaultColor = c.name(QColor::HexRgb);
      if (w.tools.pointColorBtn && w.settings.defaultPointColor.isEmpty())
        w.updateColorSwatch(w.tools.pointColorBtn, c);
      w.parts.styleControls.onLineStyleControlChanged();
    };
    support::wireColorChip(w.tools.lineColorBtn, [this, applyLineColor] {
      const QColor c =
          support::pickColorAnimated(w.tools.lineColorValue, &w, "Line color", w.tools.lineColorBtn);
      if (c.isValid()) applyLineColor(c);
    }, [applyLineColor] { applyLineColor(QColor(defaultVisuals::table().color)); });
    // Picking the line colour again stores empty (inherit), so later line-colour changes keep
    // carrying the points.
    const auto applyPointColor = [this](const QColor& c) {
      w.settings.defaultPointColor =
          (c.rgb() == w.tools.lineColorValue.rgb()) ? QString() : c.name(QColor::HexRgb);
      w.updateColorSwatch(w.tools.pointColorBtn, w.parts.styleControls.effectiveDefaultPointColor());
      w.parts.styleControls.onLineStyleControlChanged();
    };
    QObject::connect(w.tools.pointColorBtn, &QToolButton::clicked, &w, [this, applyPointColor] {
      const QColor c = support::pickColorAnimated(w.parts.styleControls.effectiveDefaultPointColor(), &w,
                                                  "Point color", w.tools.pointColorBtn);
      if (c.isValid()) applyPointColor(c);
    });
    // A chip dragged onto another hands it its colour, which lands through that chip's own pick.
    support::installColorDrag(w.tools.lineColorBtn, {[this] { return w.tools.lineColorValue; }, applyLineColor});
    support::installColorDrag(w.tools.pointColorBtn,
                              {[this] { return w.parts.styleControls.effectiveDefaultPointColor(); }, applyPointColor});
    support::installColorDrag(w.tools.filterColorBtn, {[this] { return w.tools.filterColorValue; },
                                                       [this](const QColor& c) { w.applyTintColor(c); }});
    // drawingApp.js:156-178
    QObject::connect(w.tools.lineThickness, QOverload<int>::of(&QSpinBox::valueChanged), &w,
                     [this](int v) {
                       w.settings.defaultThickness = v;
                       if (w.ctxMenu.thickSpin) {  // keep the context-menu spinbox in sync (two-way)
                         QSignalBlocker b(w.ctxMenu.thickSpin);
                         w.ctxMenu.thickSpin->setValue(v);
                       }
                       w.parts.styleControls.onLineStyleControlChanged();
                     });
    QObject::connect(w.tools.pointSize, QOverload<int>::of(&QSpinBox::valueChanged), &w,
                     [this](int v) {
                       w.settings.defaultPointSize = v;
                       if (w.ctxMenu.pointSpin) {  // keep the context-menu spinbox in sync (two-way)
                         QSignalBlocker b(w.ctxMenu.pointSpin);
                         w.ctxMenu.pointSpin->setValue(v);
                       }
                       w.parts.styleControls.onLineStyleControlChanged();
                     });
    QObject::connect(w.tools.lineStyle, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     &w, [this](int) {
                       w.parts.styleControls.applyLineStyle(w.tools.lineStyle->currentData().toString());
                     });

    // drawingApp.js:228-238; shared with the context menu.
    QObject::connect(w.tools.imageFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     &w, [this](int) {
                       w.applyImageFilter(w.tools.imageFilter->currentData().toString());
                     });
    // Hover-preview repaints only; leaving without a pick reverts (searchCombo setPreview, browser
    // twin).
    static_cast<SearchComboBox*>(w.tools.imageFilter)->setPreview([this](const QString& mode) {
      w.canvas->setImageFilter(mode, w.tools.filterColorValue);
    });
    // drawingApp.js:240-249
    QObject::connect(w.tools.filterColorBtn, &QToolButton::clicked, &w, [this] {
      const QColor c =
          support::pickColorAnimated(w.tools.filterColorValue, &w, "Tint color", w.tools.filterColorBtn);
      if (c.isValid()) w.applyTintColor(c);
    });
  }
}  // namespace stencil::gui

