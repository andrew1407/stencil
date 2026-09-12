// The canvas context menu's persistent action set — port of browser/js/ui/contextMenu.js
// wire(). This file holds the shared hosted-row helpers, the phase chain itself, and the
// style group (section captions, the point/thickness spinboxes, the filter radios).
#include "mainWindow.hpp"
#include "canvasWidget.hpp"
#include "numericInput.hpp"
#include "menuRowPolish.hpp"
#include "../support/guiHelpers.hpp"
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

  // Persistent context-menu submenu actions. Port of the wiring done once
  // in browser/js/ui/contextMenu.js wire() (~112-605): the instant line/rect
  // items, and the Style / Image-Filter / Tooltip submenus.
  // Built once and reused on every right-click; showContextMenu() only re-syncs
  // their checked/enabled/visible state before exec (mirroring syncState ~239).
  // One context-menu row: a hosted QWidget at the shared indent, its layout returned for the
  // caller to fill (the host is lay->parentWidget()), and `act` set to the QWidgetAction that
  // carries it. Hosted rather than a plain QAction so a click inside keeps the menu open.
  QHBoxLayout* MainWindow::makeContextMenuRow(QWidgetAction*& act, int topM, int botM) {
    auto* w = new QWidget(this);
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(14, topM, 14, botM);
    act = new QWidgetAction(this);
    act->setDefaultWidget(w);
    return lay;
  }

  // A checkbox row in one of those. The button expands across the row so its own hit area —
  // which toggles AND consumes the click, keeping the menu open — covers the whole strip,
  // label and trailing space included, not just the indicator.
  void MainWindow::addContextCheckRow(const QString& text, bool checked, QCheckBox*& box,
                                     QWidgetAction*& act) {
    auto* lay = makeContextMenuRow(act);
    box = new QCheckBox(text, lay->parentWidget());
    box->setChecked(checked);
    box->setSizePolicy(QSizePolicy::Expanding, box->sizePolicy().verticalPolicy());
    lay->addWidget(box);
  }

  // Port of browser/js/ui/contextMenu.js wire(): the persistent actions and QWidgetActions the
  // nested right-click menu reuses on every open. Phases in call order.
  void MainWindow::buildContextActions() {
    buildDrawNowActions();
    buildContextStyleActions();
    buildContextTooltipActions();
    buildUnitActions();
  }

  void MainWindow::buildContextStyleActions() {
    // Style submenu (contextMenu.js:39-57): spinboxes in QWidgetActions + an
    // exclusive line-style radio group; all push canvas DEFAULTS only.
    // Context-menu sub-label (browser parity: contextMenu.js's .ctx-sub-label — "Image",
    // "Line Style", …): plain muted caption text, same row indent as every other row via
    // makeContextMenuRow, NOT QMenu::addSection() — a section draws its OWN separator line, which
    // the browser's caption never has (mainWindow.hpp's member comment explains why).
    auto makeSectionLabel = [this](const QString& text) -> QWidgetAction* {
      QWidgetAction* act = nullptr;
      auto* lay = makeContextMenuRow(act, 6, 2);
      auto* label = new QLabel(text.toUpper(), lay->parentWidget());
      label->setObjectName(QStringLiteral("panelSectionHeader"));
      lay->addWidget(label);
      return act;
    };
    secImageAct_ = makeSectionLabel("Image");
    secLayoutJsonAct_ = makeSectionLabel("Layout (JSON)");
    secLineStyleAct_ = makeSectionLabel("Line Style");
    secFilterAct_ = makeSectionLabel("Filter");
    secCoordFormulasAct_ = makeSectionLabel("Coordinate Formulas");
    secShowInTooltipAct_ = makeSectionLabel("Show in Tooltip");

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
    styleRow("Point Size", pointSpin_, 1, 30, pointSizeAction_);
    styleRow("Line Thickness", thickSpin_, 1, 20, thicknessAction_);
    // Point / thickness commit on change (contextMenu.js:467-491): defaults +
    // persist + canvas redraw via setDefaults.
    connect(pointSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) {
              settings_.defaultPointSize = v;
              if (pointSize_) {
                QSignalBlocker b(pointSize_);
                pointSize_->setValue(v);  // keep toolbar control in sync
              }
              onLineStyleControlChanged();
            });
    connect(thickSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) {
              settings_.defaultThickness = v;
              if (lineThickness_) {
                QSignalBlocker b(lineThickness_);
                lineThickness_->setValue(v);
              }
              onLineStyleControlChanged();
            });

    // Line-style radio group (contextMenu.js:51-55, 494-501).
    lineStyleGroup_ = new QActionGroup(this);
    lineStyleGroup_->setExclusive(true);
    auto mkStyle = [this](const QString& text, const QString& value) {
      auto* a = new QAction(text, this);
      a->setCheckable(true);
      a->setData(value);
      lineStyleGroup_->addAction(a);
      connect(a, &QAction::triggered, this,
              [this, value] { applyLineStyle(value); });
      return a;
    };
    actStyleSolid_ = mkStyle("Solid", "solid");
    actStyleDashed_ = mkStyle("Dashed", "dashed");
    actStyleDotted_ = mkStyle("Dotted", "dotted");

    // Image Filter submenu (contextMenu.js:59-74, 504-526). Exclusive radio
    // group + a custom-tint picker action shown only when "custom" is active.
    filterButtons_ = new QButtonGroup(this);
    filterButtons_->setExclusive(true);
    auto mkFilter = [this](const QString& text, const QString& value) {
      QWidgetAction* act;
      auto* lay = makeContextMenuRow(act);
      auto* rb = new QRadioButton(text, lay->parentWidget());
      rb->setProperty("filterValue", value);
      filterButtons_->addButton(rb);
      // Expand across the row so the whole strip is the radio's hit area (label + trailing space),
      // and the radio itself consumes the click so the menu stays open.
      rb->setSizePolicy(QSizePolicy::Expanding, rb->sizePolicy().verticalPolicy());
      lay->addWidget(rb);
      // toggled(true) fires for the newly-selected radio; applyImageFilter is a no-op-safe re-set.
      // The tint row appears/disappears with the Custom Tint pick while the menu is up
      // (browser parity: contextMenu.js toggles .ctx-tint-visible on the radio change) —
      // syncContextActions() only sets it for the NEXT open. QMenu re-lays itself out on
      // an action's visibility change, so the flyout grows/shrinks in place.
      connect(rb, &QRadioButton::toggled, this, [this, value](bool on) {
        if (!on) return;
        applyImageFilter(value);
        if (tintColorAction_) tintColorAction_->setVisible(value == "custom");
      });
      return act;
    };
    actFilterNone_ = mkFilter("None", "none");
    actFilterBW_ = mkFilter("Black && White", "bw");
    actFilterSepia_ = mkFilter("Sepia", "sepia");
    actFilterInvert_ = mkFilter("Invert", "invert");
    actFilterContour_ = mkFilter("Contour", "contour");
    actFilterCustom_ = mkFilter("Custom Tint", "custom");
    // Tint color picker (contextMenu.js:518-526): pick the duotone tint, persist,
    // re-apply when the active filter is custom.
    tintColorAction_ = new QAction("Tint Color…", this);
    connect(tintColorAction_, &QAction::triggered, this, [this] {
      // Anchor on the toolbar tint swatch (visible whenever the custom filter is on);
      // revealDialog falls back per its contract when it is hidden.
      const QColor c =
          support::pickColorAnimated(filterColorValue_, this, "Tint color", filterColorBtn_);
      if (c.isValid()) applyTintColor(c);
    });

    // Tooltip toggles (contextMenu.js:96-107, 546-557). Hosted as real QCheckBoxes
    // in QWidgetActions (like the point/thickness spinbox rows) so a click flips them
    // WITHOUT dismissing the menu — the browser's context menu likewise keeps its inline
    // checkboxes/sliders live — and so they render as checkboxes, not the action's icon.
    // Per-row visibility is backed by the MainWindow booleans (consumed in onHoverDetail).
  }

}  // namespace stencil::gui
