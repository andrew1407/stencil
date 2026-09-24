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
#include "../../support/control/reveal/controlReveal.hpp"   // section buttons come and go as sand
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

// MainWindow's toolbar assembly: the third row and the style/formula wiring.

namespace stencil::gui {

  void MainWindow::buildPageFormulaToolbar() {
    QToolBar* row = toolRow();

    // Mirrors View ▸ Units; data carries the canonical code, both route through applyUnits().
    units.unitCombo = new SearchComboBox(this, /*searchable=*/false);
    units.unitCombo->addItem("cm", "cm");
    units.unitCombo->addItem("in", "in");
    units.unitCombo->setToolTip("Display units (cm / inches)");   // its own caption
    connect(units.unitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { applyUnits(units.unitCombo->currentData().toString()); });
    addWrappedSeparator(row);
    // The browser's .zoom-controls, Fit first (browser twin: #zoom-fit); the steppers are the same
    // actions the View menu and Alt+↑/↓ drive.
    addWrapped(row,
        makeToolSection("Zoom", { actZoomOut, actZoomIn }, { zoom }, { zoomFitBtn }));
    addWrappedSeparator(row);
    // Built before the section so they can go inside it, or they centre on the toolbar's full
    // height and lose the row's baseline.
    units.customGroup = new QWidget(this);
    {
      auto* cl = new QHBoxLayout(units.customGroup);
      cl->setContentsMargins(2, 0, 0, 0);
      cl->setSpacing(5);   // the section row's own gap
      units.customW = new ExprDoubleSpinBox(units.customGroup);
      units.customW->setRange(0.1, 500.0);  // browser LIMITS custom page bounds
      units.customW->setSingleStep(0.1);
      units.customW->setDecimals(1);
      units.customW->setValue(21.0);
      units.customW->setToolTip("Custom page width in the selected units");
      // Compact like the browser's width:96px (toolbar.js), trimmed so SETTINGS always fits after
      // DATA.
      units.customW->setMaximumWidth(76);
      // Ignored + an explicit minimum makes 56 the floor: a spin box's own minimumSizeHint (89) is
      // one the toolbar layout cannot go under.
      units.customW->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
      units.customW->setMinimumWidth(56);
      units.customH = new ExprDoubleSpinBox(units.customGroup);
      units.customH->setRange(0.1, 500.0);
      units.customH->setSingleStep(0.1);
      units.customH->setDecimals(1);
      units.customH->setValue(29.7);
      units.customH->setToolTip("Custom page height in the selected units");
      units.customH->setMaximumWidth(76);
      units.customH->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
      units.customH->setMinimumWidth(56);
      cl->addWidget(units.customW, 0, Qt::AlignVCenter);
      cl->addWidget(new QLabel("×", units.customGroup), 0, Qt::AlignVCenter);
      cl->addWidget(units.customH, 0, Qt::AlignVCenter);
      // No unit suffix (browser twin): the units combo already says cm / in.
    }
    units.customGroup->setVisible(false);   // revealed by the "custom" page size
    // One named section, no inline captions (browser twin): each combo spells its own answer out.
    addWrapped(row, makeToolSection("Page", {}, { units.pageSize, units.unitCombo, units.customGroup }));

    allowFormulas = new QCheckBox("𝑓(x,y)", this);
    // Accent pill toggle (theme.cpp QCheckBox#formulaPill), matching the browser toolbar.
    allowFormulas->setObjectName("formulaPill");
    allowFormulas->setToolTip("Transform page coordinates with a formula f(x,y)");
    // Content-sized: a stretchable pill swallowed the row's leftover width while its click rect
    // stayed at the left.
    allowFormulas->setSizePolicy(QSizePolicy::Fixed, allowFormulas->sizePolicy().verticalPolicy());
    // f(x,y) is its own fenced section (the browser writes a .ctrl-sep). makeToolSection gives every
    // control one height and Qt::AlignVCenter, like the browser's flex row.
    buildFormulaFields();
    addWrappedSeparator(row);
    // Content-sized, no expanding tail: the browser packs sections left and leaves the slack at
    // the end of the row.
    QWidget* formulaSection = makeToolSection("Formula", {}, { allowFormulas, formulaGroup });
    formulaSection->setSizePolicy(QSizePolicy::Maximum, formulaSection->sizePolicy().verticalPolicy());
    addWrapped(row, formulaSection);
    addWrappedSeparator(row);
    // Copy leads, then the two file moves down, then up (browser twin: toolbar.js Data row).
    addWrapped(row, makeToolSection("Data",
                                   {actScript, actCopyLayout, actDownloadJson, actUploadJson, actClearProject}));
    addWrappedSeparator(row);
    // The browser's last cluster in order; every button drives the existing QAction. actAccent is
    // the logo's own popover, no toolbar icon.
    settingsSection = makeToolSection(
        "Settings", {actIncognito, actFullscreen, actTheme, actShortcuts, actSettings, actInfo});
    addWrapped(row, settingsSection);
    formulaGroup->setVisible(false);   // revealed by the pill (setFormulaFieldsVisible)
  }
}  // namespace stencil::gui

