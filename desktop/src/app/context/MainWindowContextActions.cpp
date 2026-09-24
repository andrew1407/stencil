// The canvas context menu's persistent action set — port of browser/js/ui/contextMenu/contextMenu.js wire().
#include "../../support/control/dblReset.hpp"
#include "MainWindow.hpp"
#include "CanvasWidget.hpp"
#include "numericInput.hpp"
#include "menuRowPolish.hpp"
#include "../../support/guiHelpers.hpp"
#include <QAction>
#include <QActionGroup>
#include <QButtonGroup>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QSpinBox>
#include <QToolButton>
#include <QWidgetAction>

namespace stencil::gui {

  // Built once and reused on every right-click; showContextMenu() re-syncs state before exec (syncState ~239).
  // A hosted QWidget row at the shared indent; hosted so a click inside keeps the menu open.
  QHBoxLayout* MainWindow::makeContextMenuRow(QWidgetAction*& act, int topM, int botM) {
    auto* w = new QWidget(this);
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(14, topM, 14, botM);
    act = new QWidgetAction(this);
    act->setDefaultWidget(w);
    return lay;
  }

  // The button expands across the row so its click-consuming hit area covers the whole strip.
  void MainWindow::addContextCheckRow(const QString& text, bool checked, QCheckBox*& box,
                                     QWidgetAction*& act) {
    auto* lay = makeContextMenuRow(act);
    box = new QCheckBox(text, lay->parentWidget());
    box->setChecked(checked);
    box->setSizePolicy(QSizePolicy::Expanding, box->sizePolicy().verticalPolicy());
    lay->addWidget(box);
  }

  // Phases in call order.
  void MainWindow::buildContextActions() {
    buildDrawNowActions();
    buildContextStyleActions();
    buildContextTooltipActions();
    buildUnitActions();
    // What a double-click on a menu row restores (support/control/dblReset.hpp).
    const Settings d;
    for (const auto& [o, v] : std::initializer_list<std::pair<QObject*, bool>>{
             {actShowPoints, d.showPoints}, {actShowLines, d.showLines}, {tooltipEnableCheck, d.tooltipEnabled},
             {ttPageCheck, d.tooltipShowPage}, {ttScreenCheck, d.tooltipShowScreen},
             {ttCoordsCheck, d.tooltipShowCoords}, {ctxAllowFormulas, d.allowFormulas}})
      support::setResetDefault(o, v);
  }

  void MainWindow::buildContextStyleActions() {
    // contextMenu.js:39-57; all push canvas DEFAULTS only. Captions are plain rows via makeContextMenuRow, NOT
    // QMenu::addSection() — a section draws its OWN separator line, which the browser's caption never has.
    auto makeSectionLabel = [this](const QString& text) -> QWidgetAction* {
      QWidgetAction* act = nullptr;
      auto* lay = makeContextMenuRow(act, 6, 2);
      auto* label = new QLabel(text.toUpper(), lay->parentWidget());
      label->setObjectName(QStringLiteral("panelSectionHeader"));
      lay->addWidget(label);
      return act;
    };
    secImageAct = makeSectionLabel("Image");
    secLayoutJsonAct = makeSectionLabel("Layout (JSON)");
    secLineStyleAct = makeSectionLabel("Line Style");
    secFilterAct = makeSectionLabel("Filter");
    secCoordFormulasAct = makeSectionLabel("Coordinate Formulas");
    secShowInTooltipAct = makeSectionLabel("Show in Tooltip");

    auto styleRow = [this](const QString& label, QSpinBox*& spin, int lo, int hi,
                                         QWidgetAction*& act) {
      auto* lay = makeContextMenuRow(act);
      auto* w = lay->parentWidget();
      lay->addWidget(new QLabel(label, w));
      spin = new ExprSpinBox(w);
      spin->setRange(lo, hi);
      lay->addStretch(1);
      lay->addWidget(spin);
    };
    styleRow("Point Size", pointSpin, 1, 30, pointSizeAction);
    styleRow("Line Thickness", thickSpin, 1, 20, thicknessAction);
    // contextMenu.js:467-491
    connect(pointSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) {
              settings.defaultPointSize = v;
              if (pointSize) {
                QSignalBlocker b(pointSize);
                pointSize->setValue(v);  // keep toolbar control in sync
              }
              onLineStyleControlChanged();
            });
    connect(thickSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) {
              settings.defaultThickness = v;
              if (lineThickness) {
                QSignalBlocker b(lineThickness);
                lineThickness->setValue(v);
              }
              onLineStyleControlChanged();
            });

    // contextMenu.js:51-55, 494-501
    lineStyleGroup = new QActionGroup(this);
    lineStyleGroup->setExclusive(true);
    auto mkStyle = [this](const QString& text, const QString& value) {
      auto* a = new QAction(text, this);
      a->setCheckable(true);
      a->setData(value);
      lineStyleGroup->addAction(a);
      connect(a, &QAction::triggered, this,
              [this, value] { applyLineStyle(value); });
      return a;
    };
    actStyleSolid = mkStyle("Solid", "solid");
    actStyleDashed = mkStyle("Dashed", "dashed");
    actStyleDotted = mkStyle("Dotted", "dotted");

    // contextMenu.js:59-74, 504-526; the tint picker shows only when "custom" is active.
    filterButtons = new QButtonGroup(this);
    filterButtons->setExclusive(true);
    auto mkFilter = [this](const QString& text, const QString& value) {
      QWidgetAction* act;
      auto* lay = makeContextMenuRow(act);
      auto* rb = new QRadioButton(text, lay->parentWidget());
      rb->setProperty("filterValue", value);
      filterButtons->addButton(rb);
      // Expand across the row so the whole strip is the hit area; the radio consumes the click.
      rb->setSizePolicy(QSizePolicy::Expanding, rb->sizePolicy().verticalPolicy());
      lay->addWidget(rb);
      // The tint row toggles while the menu is up (contextMenu.js .ctx-tint-visible); QMenu re-lays itself on a visibility change.
      connect(rb, &QRadioButton::toggled, this, [this, value](bool on) {
        if (!on) return;
        applyImageFilter(value);
        if (tintColorAction) tintColorAction->setVisible(value == "custom");
      });
      return act;
    };
    actFilterNone = mkFilter("None", "none");
    actFilterBW = mkFilter("Black && White", "bw");
    actFilterSepia = mkFilter("Sepia", "sepia");
    actFilterInvert = mkFilter("Invert", "invert");
    actFilterContour = mkFilter("Contour", "contour");
    actFilterCustom = mkFilter("Custom Tint", "custom");
    // contextMenu.js:518-526
    tintColorAction = new QAction("Tint Color…", this);
    connect(tintColorAction, &QAction::triggered, this, [this] {
      // Anchor on the toolbar tint swatch; revealDialog falls back when it is hidden.
      const QColor c =
          support::pickColorAnimated(filterColorValue, this, "Tint color", filterColorBtn);
      if (c.isValid()) applyTintColor(c);
    });

    // contextMenu.js:96-107, 546-557. Real QCheckBoxes in QWidgetActions so a click flips them WITHOUT dismissing the menu.
  }

}  // namespace stencil::gui
