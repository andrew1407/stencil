// The canvas context menu's persistent action set — port of browser/js/ui/contextMenu/contextMenu.js wire().
#include "../../support/control/dblReset.hpp"
#include "../../support/control/lineLimits.hpp"
#include "MainWindow.hpp"
#include "CanvasContextMenu.hpp"
#include "numericInput.hpp"
#include "modalReveal.hpp"
#include <QActionGroup>
#include <QButtonGroup>
#include <QRadioButton>
#include <QToolButton>
#include <QHBoxLayout>
#include <QLabel>
#include <QWidgetAction>

namespace stencil::gui {

  // Built once and reused on every right-click; showContextMenu() re-syncs state before exec (syncState ~239).
  // A hosted QWidget row at the shared indent; hosted so a click inside keeps the menu open.
  QHBoxLayout* CanvasContextMenu::makeContextMenuRow(QWidgetAction*& act, int topM, int botM) {
    auto* row = new QWidget(&w);
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(14, topM, 14, botM);
    act = new QWidgetAction(&w);
    act->setDefaultWidget(row);
    return lay;
  }

  // The button expands across the row so its click-consuming hit area covers the whole strip.
  void CanvasContextMenu::addContextCheckRow(const QString& text, bool checked, QCheckBox*& box,
                                     QWidgetAction*& act) {
    auto* lay = makeContextMenuRow(act);
    box = new QCheckBox(text, lay->parentWidget());
    box->setChecked(checked);
    box->setSizePolicy(QSizePolicy::Expanding, box->sizePolicy().verticalPolicy());
    lay->addWidget(box);
  }

  // Phases in call order.
  void CanvasContextMenu::buildContextActions() {
    buildDrawNowActions();
    buildContextStyleActions();
    buildContextTooltipActions();
    buildUnitActions();
    // What a double-click on a menu row restores (support/control/dblReset.hpp).
    const Settings d;
    for (const auto& [o, v] : std::initializer_list<std::pair<QObject*, bool>>{
             {w.acts.showPoints, d.showPoints}, {w.acts.showLines, d.showLines}, {w.ctxMenu.tooltipEnableCheck, d.tooltipEnabled},
             {w.ctxMenu.ttPageCheck, d.tooltipShowPage}, {w.ctxMenu.ttScreenCheck, d.tooltipShowScreen},
             {w.ctxMenu.ttCoordsCheck, d.tooltipShowCoords}, {w.ctxMenu.allowFormulas, d.allowFormulas}})
      support::setResetDefault(o, v);
  }

  void CanvasContextMenu::buildContextStyleActions() {
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
    w.ctxMenu.secImageAct = makeSectionLabel("Image");
    w.ctxMenu.secLayoutJsonAct = makeSectionLabel("Layout (JSON)");
    w.ctxMenu.secLineStyleAct = makeSectionLabel("Line Style");
    w.ctxMenu.secFilterAct = makeSectionLabel("Filter");
    w.ctxMenu.secCoordFormulasAct = makeSectionLabel("Coordinate Formulas");
    w.ctxMenu.secShowInTooltipAct = makeSectionLabel("Show in Tooltip");

    auto styleRow = [this](const QString& label, QSpinBox*& spin, int lo, int hi,
                                         QWidgetAction*& act) {
      auto* lay = makeContextMenuRow(act);
      auto* host = lay->parentWidget();
      lay->addWidget(new QLabel(label, host));
      spin = new ExprSpinBox(host);
      spin->setRange(lo, hi);
      lay->addStretch(1);
      lay->addWidget(spin);
    };
    const support::lineLimits::Table& limits = support::lineLimits::table();
    styleRow("Point Size", w.ctxMenu.pointSpin, limits.pointMin, limits.pointMax, w.ctxMenu.pointSizeAction);
    styleRow("Line Thickness", w.ctxMenu.thickSpin, limits.thickMin, limits.thickMax,
             w.ctxMenu.thicknessAction);
    // contextMenu.js:467-491
    QObject::connect(w.ctxMenu.pointSpin, QOverload<int>::of(&QSpinBox::valueChanged), &w,
                     [this](int v) {
                       w.settings.defaultPointSize = v;
                       if (w.tools.pointSize) {
                         QSignalBlocker b(w.tools.pointSize);
                         w.tools.pointSize->setValue(v);  // keep toolbar control in sync
                       }
                       w.parts.styleControls.onLineStyleControlChanged();
                     });
    QObject::connect(w.ctxMenu.thickSpin, QOverload<int>::of(&QSpinBox::valueChanged), &w,
                     [this](int v) {
                       w.settings.defaultThickness = v;
                       if (w.tools.lineThickness) {
                         QSignalBlocker b(w.tools.lineThickness);
                         w.tools.lineThickness->setValue(v);
                       }
                       w.parts.styleControls.onLineStyleControlChanged();
                     });

    // contextMenu.js:51-55, 494-501
    w.ctxMenu.lineStyleGroup = new QActionGroup(&w);
    w.ctxMenu.lineStyleGroup->setExclusive(true);
    auto mkStyle = [this](const QString& text, const QString& value) {
      auto* a = new QAction(text, &w);
      a->setCheckable(true);
      a->setData(value);
      w.ctxMenu.lineStyleGroup->addAction(a);
      QObject::connect(a, &QAction::triggered, &w,
                       [this, value] { w.parts.styleControls.applyLineStyle(value); });
      return a;
    };
    w.ctxMenu.styleSolid = mkStyle("Solid", "solid");
    w.ctxMenu.styleDashed = mkStyle("Dashed", "dashed");
    w.ctxMenu.styleDotted = mkStyle("Dotted", "dotted");

    // contextMenu.js:59-74, 504-526; the tint picker shows only when "custom" is active.
    w.ctxMenu.filterButtons = new QButtonGroup(&w);
    w.ctxMenu.filterButtons->setExclusive(true);
    auto mkFilter = [this](const QString& text, const QString& value) {
      QWidgetAction* act;
      auto* lay = makeContextMenuRow(act);
      auto* rb = new QRadioButton(text, lay->parentWidget());
      rb->setProperty("filterValue", value);
      w.ctxMenu.filterButtons->addButton(rb);
      // Expand across the row so the whole strip is the hit area; the radio consumes the click.
      rb->setSizePolicy(QSizePolicy::Expanding, rb->sizePolicy().verticalPolicy());
      lay->addWidget(rb);
      // The tint row toggles while the menu is up (contextMenu.js .ctx-tint-visible); QMenu re-lays itself on a visibility change.
      QObject::connect(rb, &QRadioButton::toggled, &w, [this, value](bool on) {
        if (!on) return;
        w.applyImageFilter(value);
        if (w.ctxMenu.tintColorAction) w.ctxMenu.tintColorAction->setVisible(value == "custom");
      });
      return act;
    };
    w.ctxMenu.filterNone = mkFilter("None", "none");
    w.ctxMenu.filterBW = mkFilter("Black && White", "bw");
    w.ctxMenu.filterSepia = mkFilter("Sepia", "sepia");
    w.ctxMenu.filterInvert = mkFilter("Invert", "invert");
    w.ctxMenu.filterContour = mkFilter("Contour", "contour");
    w.ctxMenu.filterCustom = mkFilter("Custom Tint", "custom");
    // contextMenu.js:518-526
    w.ctxMenu.tintColorAction = new QAction("Tint Color…", &w);
    QObject::connect(w.ctxMenu.tintColorAction, &QAction::triggered, &w, [this] {
      // Anchor on the toolbar tint swatch; revealDialog falls back when it is hidden.
      const QColor c =
          support::pickColorAnimated(w.tools.filterColorValue, &w, "Tint color", w.tools.filterColorBtn);
      if (c.isValid()) w.applyTintColor(c);
    });

    // contextMenu.js:96-107, 546-557. Real QCheckBoxes in QWidgetActions so a click flips them WITHOUT dismissing the menu.
  }

}  // namespace stencil::gui
