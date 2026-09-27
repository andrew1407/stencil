#include "MainWindow.hpp"
#include "ToolbarBuilder.hpp"
#include "mainWindowHelpers.hpp"
#include "numericInput.hpp"
#include "SearchCombo.hpp"
#include "../../support/control/WrapRow.hpp"     // rows wrap like the browser's, never overflow into "»"

#include <QCheckBox>
#include <QLabel>

// MainWindow's toolbar assembly: the third row and the style/formula wiring.

namespace stencil::gui {

  void ToolbarBuilder::buildPageFormulaToolbar() {
    QToolBar* row = toolRow();

    // Mirrors View ▸ Units; data carries the canonical code, both route through applyUnits().
    w.units.unitCombo = new SearchComboBox(&w, /*searchable=*/false);
    w.units.unitCombo->addItem("cm", "cm");
    w.units.unitCombo->addItem("in", "in");
    w.units.unitCombo->setToolTip("Display units (cm / inches)");   // its own caption
    QObject::connect(w.units.unitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &w,
                     [this](int) { w.parts.view.applyUnits(w.units.unitCombo->currentData().toString()); });
    addWrappedSeparator(row);
    // The browser's .zoom-controls, Fit first (browser twin: #zoom-fit); the steppers are the same
    // actions the View menu and Alt+↑/↓ drive.
    addWrapped(row,
        makeToolSection("Zoom", { w.acts.zoomOut, w.acts.zoomIn }, { w.zoom }, { w.tools.zoomFitBtn }));
    addWrappedSeparator(row);
    // Built before the section so they can go inside it, or they centre on the toolbar's full
    // height and lose the row's baseline.
    w.units.customGroup = new QWidget(&w);
    {
      auto* cl = new QHBoxLayout(w.units.customGroup);
      cl->setContentsMargins(2, 0, 0, 0);
      cl->setSpacing(5);   // the section row's own gap
      w.units.customW = new ExprDoubleSpinBox(w.units.customGroup);
      w.units.customW->setRange(0.1, 500.0);  // browser LIMITS custom page bounds
      w.units.customW->setSingleStep(0.1);
      w.units.customW->setDecimals(1);
      w.units.customW->setValue(21.0);
      w.units.customW->setToolTip("Custom page width in the selected units");
      // Compact like the browser's width:96px (toolbar.js), trimmed so SETTINGS always fits after
      // DATA.
      w.units.customW->setMaximumWidth(76);
      // Ignored + an explicit minimum makes 56 the floor: a spin box's own minimumSizeHint (89) is
      // one the toolbar layout cannot go under.
      w.units.customW->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
      w.units.customW->setMinimumWidth(56);
      w.units.customH = new ExprDoubleSpinBox(w.units.customGroup);
      w.units.customH->setRange(0.1, 500.0);
      w.units.customH->setSingleStep(0.1);
      w.units.customH->setDecimals(1);
      w.units.customH->setValue(29.7);
      w.units.customH->setToolTip("Custom page height in the selected units");
      w.units.customH->setMaximumWidth(76);
      w.units.customH->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
      w.units.customH->setMinimumWidth(56);
      cl->addWidget(w.units.customW, 0, Qt::AlignVCenter);
      cl->addWidget(new QLabel("×", w.units.customGroup), 0, Qt::AlignVCenter);
      cl->addWidget(w.units.customH, 0, Qt::AlignVCenter);
      // No unit suffix (browser twin): the units combo already says cm / in.
    }
    w.units.customGroup->setVisible(false);   // revealed by the "custom" page size
    // One named section, no inline captions (browser twin): each combo spells its own answer out.
    addWrapped(row, makeToolSection("Page", {}, { w.units.pageSize, w.units.unitCombo, w.units.customGroup }));

    w.tools.allowFormulas = new QCheckBox("𝑓(x,y)", &w);
    // Accent pill toggle (theme.cpp QCheckBox#formulaPill), matching the browser toolbar.
    w.tools.allowFormulas->setObjectName("formulaPill");
    w.tools.allowFormulas->setToolTip("Transform page coordinates with a formula f(x,y)");
    // Content-sized: a stretchable pill swallowed the row's leftover width while its click rect
    // stayed at the left.
    w.tools.allowFormulas->setSizePolicy(QSizePolicy::Fixed, w.tools.allowFormulas->sizePolicy().verticalPolicy());
    // f(x,y) is its own fenced section (the browser writes a .ctrl-sep). makeToolSection gives every
    // control one height and Qt::AlignVCenter, like the browser's flex row.
    buildFormulaFields();
    addWrappedSeparator(row);
    // Content-sized, no expanding tail: the browser packs sections left and leaves the slack at
    // the end of the row.
    QWidget* formulaSection = makeToolSection("Formula", {}, { w.tools.allowFormulas, w.tools.formulaGroup });
    formulaSection->setSizePolicy(QSizePolicy::Maximum, formulaSection->sizePolicy().verticalPolicy());
    addWrapped(row, formulaSection);
    addWrappedSeparator(row);
    // Copy leads, then the two file moves down, then up (browser twin: toolbar.js Data row).
    addWrapped(row, makeToolSection("Data",
                                   {w.acts.script, w.acts.copyLayout, w.acts.downloadJson, w.acts.uploadJson, w.acts.clearProject}));
    addWrappedSeparator(row);
    // The browser's last cluster in order; every button drives the existing QAction. acts.accent is
    // the logo's own popover, no toolbar icon.
    w.tools.settingsSection = makeToolSection(
        "Settings", {w.acts.incognito, w.acts.fullscreen, w.acts.theme, w.acts.shortcuts, w.acts.settings, w.acts.info});
    addWrapped(row, w.tools.settingsSection);
    w.tools.formulaGroup->setVisible(false);   // revealed by the pill (setFormulaFieldsVisible)
  }
}  // namespace stencil::gui

