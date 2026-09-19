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
#include "../support/controlReveal.hpp"   // section buttons come and go as sand
#include "../support/iconMotion.hpp"
#include "../support/ShimmerOverlay.hpp"
#include "../support/WrapRow.hpp"     // rows wrap like the browser's, never overflow into "»"

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
    units_.unitCombo = new SearchComboBox(this, /*searchable=*/false);
    units_.unitCombo->addItem("cm", "cm");
    units_.unitCombo->addItem("in", "in");
    units_.unitCombo->setToolTip("Display units (cm / inches)");   // its own caption
    connect(units_.unitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { applyUnits(units_.unitCombo->currentData().toString()); });
    addWrappedSeparator(row);
    // The browser's .zoom-controls, Fit first (browser twin: #zoom-fit); the steppers are the same
    // actions the View menu and Alt+↑/↓ drive.
    addWrapped(row,
        makeToolSection("Zoom", { actZoomOut_, actZoomIn_ }, { zoom_ }, { zoomFitBtn_ }));
    addWrappedSeparator(row);
    // Built before the section so they can go inside it, or they centre on the toolbar's full
    // height and lose the row's baseline.
    units_.customGroup = new QWidget(this);
    {
      auto* cl = new QHBoxLayout(units_.customGroup);
      cl->setContentsMargins(2, 0, 0, 0);
      cl->setSpacing(5);   // the section row's own gap
      units_.customW = new ExprDoubleSpinBox(units_.customGroup);
      units_.customW->setRange(0.1, 500.0);  // browser LIMITS custom page bounds
      units_.customW->setSingleStep(0.1);
      units_.customW->setDecimals(1);
      units_.customW->setValue(21.0);
      units_.customW->setToolTip("Custom page width in the selected units");
      // Compact like the browser's width:96px (toolbar.js), trimmed so SETTINGS always fits after
      // DATA.
      units_.customW->setMaximumWidth(76);
      // Ignored + an explicit minimum makes 56 the floor: a spin box's own minimumSizeHint (89) is
      // one the toolbar layout cannot go under.
      units_.customW->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
      units_.customW->setMinimumWidth(56);
      units_.customH = new ExprDoubleSpinBox(units_.customGroup);
      units_.customH->setRange(0.1, 500.0);
      units_.customH->setSingleStep(0.1);
      units_.customH->setDecimals(1);
      units_.customH->setValue(29.7);
      units_.customH->setToolTip("Custom page height in the selected units");
      units_.customH->setMaximumWidth(76);
      units_.customH->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
      units_.customH->setMinimumWidth(56);
      cl->addWidget(units_.customW, 0, Qt::AlignVCenter);
      cl->addWidget(new QLabel("×", units_.customGroup), 0, Qt::AlignVCenter);
      cl->addWidget(units_.customH, 0, Qt::AlignVCenter);
      // No unit suffix (browser twin): the units combo already says cm / in.
    }
    units_.customGroup->setVisible(false);   // revealed by the "custom" page size
    // One named section, no inline captions (browser twin): each combo spells its own answer out.
    addWrapped(row, makeToolSection("Page", {}, { units_.pageSize, units_.unitCombo, units_.customGroup }));

    allowFormulas_ = new QCheckBox("𝑓(x,y)", this);
    // Accent pill toggle (theme.cpp QCheckBox#formulaPill), matching the browser toolbar.
    allowFormulas_->setObjectName("formulaPill");
    allowFormulas_->setToolTip(
        "Enable x/y coordinate transform formulas applied to the points table");
    // Content-sized: a stretchable pill swallowed the row's leftover width while its click rect
    // stayed at the left.
    allowFormulas_->setSizePolicy(QSizePolicy::Fixed, allowFormulas_->sizePolicy().verticalPolicy());
    // f(x,y) is its own fenced section (the browser writes a .ctrl-sep). makeToolSection gives every
    // control one height and Qt::AlignVCenter, like the browser's flex row.
    buildFormulaFields();
    addWrappedSeparator(row);
    // Content-sized, no expanding tail: the browser packs sections left and leaves the slack at
    // the end of the row.
    QWidget* formulaSection = makeToolSection("Formula", {}, { allowFormulas_, formulaGroup_ });
    formulaSection->setSizePolicy(QSizePolicy::Maximum, formulaSection->sizePolicy().verticalPolicy());
    addWrapped(row, formulaSection);
    addWrappedSeparator(row);
    // Copy leads, then the two file moves down, then up (browser twin: toolbar.js Data row).
    addWrapped(row, makeToolSection("Data",
                                   {actScript_, actCopyLayout_, actDownloadJson_, actUploadJson_, actClearProject_}));
    addWrappedSeparator(row);
    // The browser's last cluster in order; every button drives the existing QAction. actAccent_ is
    // the logo's own popover, no toolbar icon.
    settingsSection_ = makeToolSection(
        "Settings", {actIncognito_, actFullscreen_, actTheme_, actShortcuts_, actSettings_, actInfo_});
    addWrapped(row, settingsSection_);
    formulaGroup_->setVisible(false);   // revealed by the pill (setFormulaFieldsVisible)
  }
}  // namespace stencil::gui

