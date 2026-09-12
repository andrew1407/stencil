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

// MainWindow's toolbar assembly: the header row, the three tool rows and their
// sections, and the style/formula wiring. Split from mainWindow.cpp; same class,
// definitions only.

namespace stencil::gui {

  void MainWindow::buildPageFormulaToolbar() {
    // Zoom · Page · Formula · Data · Settings — the tail of the browser's sequence.
    QToolBar* row = toolRow();

    // Units switch on the toolbar (mirrors View ▸ Units, kept in sync). data
    // carries the canonical code; both surfaces route through applyUnits().
    units_.unitCombo = new SearchComboBox(this, /*searchable=*/false);
    units_.unitCombo->addItem("cm", "cm");
    units_.unitCombo->addItem("in", "in");
    units_.unitCombo->setToolTip("Display units (cm / inches)");   // its own caption
    connect(units_.unitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { applyUnits(units_.unitCombo->currentData().toString()); });
    // ZOOM and PAGE follow VIEW, keeping the browser's sequence.
    addWrappedSeparator(row);
    // The browser's cluster exactly: [−] [+] [value %] [fit] (toolbar.js .zoom-controls).
    // The steppers are the same two actions the View menu and Alt+↑/↓ drive, so the three
    // ways to zoom stay one thing; the editable combo stands in for the browser's number
    // field with its preset menu, and Fit closes the row there too.
    // Fit LEADS the row, ahead of − and + (user decision; browser twin: #zoom-fit first in
    // .zoom-controls): it is the one that puts the whole image back on screen, and the two
    // steppers follow it with the % field.
    addWrapped(row,
        makeToolSection("Zoom", { actZoomOut_, actZoomIn_ }, { zoom_ }, { zoomFitBtn_ }));
    addWrappedSeparator(row);
    // Inline custom W x H inputs, shown only for the "custom" page size. Built BEFORE
    // the section so they can go INSIDE it: added straight to the toolbar they were centred
    // on its full height while the two combos sat under the section caption, so the row
    // never shared a baseline (the same trap the formula fields fell into, below).
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
      // Width-tightening: keep the custom-page spinboxes compact
      // (browser style width:96px, toolbar.js:110/112) — trimmed a little further
      // with the rest of this row so the SETTINGS cluster always fits after DATA.
      units_.customW->setMaximumWidth(76);
      // Capped at 76 as before, but the row may squeeze them: a spin box's own
      // minimumSizeHint (89) is a floor the toolbar layout cannot go under, and with the
      // zoom steppers added this row asked for more than a 1000px window has. Ignored +
      // an explicit minimum makes 56 the floor instead; the maximum above still stops
      // them growing. Both boxes only show at all for a CUSTOM page size.
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
      // No unit suffix after the boxes (user decision; browser twin: the same span is gone
      // from toolbar.js) — the units combo two controls left already says cm / in.
    }
    units_.customGroup->setVisible(false);   // revealed by the "custom" page size
    // One NAMED section, like every group in the main row and like the browser's PAGE
    // cluster. Neither combo carries an inline caption (user decision, browser twin):
    // each spells its own answer out ("A4 (21 × 29.7 cm)", "cm").
    addWrapped(row, makeToolSection("Page", {}, { units_.pageSize, units_.unitCombo, units_.customGroup }));

    // Inline formula controls: an enable checkbox + fx/fy inputs + error.
    allowFormulas_ = new QCheckBox("𝑓(x,y)", this);
    // Styled as an accent PILL toggle (theme.cpp QCheckBox#formulaPill): accent outline + text
    // when off, accent-filled with contrasting text when on — matching the browser toolbar.
    allowFormulas_->setObjectName("formulaPill");
    allowFormulas_->setToolTip(
        "Enable x/y coordinate transform formulas applied to the points table");
    // Content-sized: this section takes the row's leftover width (below), and a stretchable
    // pill swallowed it whenever the fields were hidden — a wide chip whose clickable area
    // (QCheckBox's click rect) stayed at the left, so half of it did nothing.
    allowFormulas_->setSizePolicy(QSizePolicy::Fixed, allowFormulas_->sizePolicy().verticalPolicy());
    // f(x,y) sits between PAGE and DATA, where the browser puts it (its PAGE cluster
    // carries the pill and the inputs inline; here they are their own named section, as
    // every cluster on this row is), fenced by its own separator like every neighbour —
    // it was the one section running straight on from PAGE (the browser writes a .ctrl-sep).
    // The x/y inputs belong to the SAME section as the pill that reveals them. Added
    // straight to the toolbar instead, they were centred on the toolbar's full height
    // while the pill sat under the section's caption — so the two never shared a
    // baseline. makeToolSection gives every control in the row one height and
    // Qt::AlignVCenter, which is what the browser's flex row does.
    buildFormulaFields();
    addWrappedSeparator(row);
    // Content-sized, with no expanding tail: DATA and SETTINGS follow it on this row now,
    // and a cluster that took the row's leftover width would shove them to the far edge —
    // the browser packs its sections left and leaves the slack at the END of the row.
    QWidget* formulaSection = makeToolSection("Formula", {}, { allowFormulas_, formulaGroup_ });
    formulaSection->setSizePolicy(QSizePolicy::Maximum, formulaSection->sizePolicy().verticalPolicy());
    addWrapped(row, formulaSection);
    addWrappedSeparator(row);
    // Data then Settings close the row, mirroring the browser's last one
    // (Zoom · Page · Formula · Data · Settings). Incognito lives in Settings.
    // Copy leads, then the two FILE moves (down, then up) — the pair reads as one gesture
    // in two directions (user decision; browser twin: toolbar.js's Data row).
    addWrapped(row, makeToolSection("Data",
                                   {actCopyLayout_, actDownloadJson_, actUploadJson_, actClearProject_}));
    addWrappedSeparator(row);
    // Settings mirrors the browser's last cluster in order: theme · fullscreen ·
    // fullscreen · theme · gear (Shortcuts) · palette (Visuals) · info (Help). Every button
    // drives the existing QAction (keeps toolbar and menu bar in step). actAccent_
    // is NOT in this row — it's the logo's own right-click/dblclick popover, with
    // no toolbar icon of its own in the browser either.
    settingsSection_ = makeToolSection(
        "Settings", {actIncognito_, actFullscreen_, actTheme_, actShortcuts_, actSettings_, actInfo_});
    addWrapped(row, settingsSection_);
    formulaGroup_->setVisible(false);   // revealed by the pill (setFormulaFieldsVisible)
    // (Theme / Incognito / Settings / Info are NOT menu-bar-only any more: they
    // are the SETTINGS section that closes this row, mirroring the browser's last
    // cluster. The actions are shared, so both surfaces stay in step.)
  }
}  // namespace stencil::gui

