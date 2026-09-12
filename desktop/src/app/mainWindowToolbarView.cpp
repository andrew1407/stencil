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

  void MainWindow::buildDrawViewToolbar() {
    // Draw · View, continuing the one run — the wrap points are the layout's to choose.
    QToolBar* row = toolRow();
    addWrappedSeparator(row);
    addWrapped(row, makeToolSection("Draw", {actStartDraw_}, {drawModeBtn_}));
    addWrappedSeparator(row);
    // Compare view combo (browser toolbar View section): hold the edit against the
    // untouched original. Kept in sync with the View → Compare submenu radio set.
    compareCombo_ = new SearchComboBox(this, /*searchable=*/false);
    // Short labels: the closed combo is sized by its widest item, and the verbose ones left a
    // wide empty box beside the clear-lines button. The tooltip below spells each out.
    compareCombo_->addItem("None", "none");
    compareCombo_->addItem("Original", "original");
    compareCombo_->addItem(QString::fromUtf8("Split ↔"), "vertical");
    compareCombo_->addItem(QString::fromUtf8("Split ↕"), "horizontal");
    // The browser's words (browser/js/ui/toolbar.js #compare-mode): one bulleted row per
    // mode and the peek gesture as a parenthesised hint. The cycle chord is Cycle Compare
    // View's, appended as the trailing "(…)" tipContent draws as the heading's keycap —
    // naming it in the prose as well would print it twice — and the greyed-out reason
    // joins while there is nothing to compare.
    setTipBase(compareCombo_,
               "Compare with original\n"
               "• None — normal editing\n"
               "• Original — the original only (crop + rotation)\n"
               "• Vertical split — original left, edit right\n"
               "• Horizontal split — original top, edit bottom\n"
               "(hold Alt+Shift+O to peek)");
    setTipHotkey(compareCombo_, actCycleCompare_);
    setTipReason(compareCombo_, "Load an image to compare");
    // Compare view combo → route through the shared setter (syncs canvas + View
    // submenu). Wired HERE, not alongside the toolbar's other combo connects
    // (buildStyleToolbar) — that function runs BEFORE this one (buildToolbar's own
    // call order), so a connect() there would target a still-null compareCombo_ and
    // silently do nothing (Qt warns "invalid nullptr parameter" and drops it): every
    // row in the popup looked selectable but never touched the canvas (reported).
    connect(compareCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
              setCompareModeUi(compareCombo_->currentData().toString());
            });
    // Hover-preview each compare mode on the canvas (repaint only — no control re-gating);
    // leaving the list or closing without a pick reverts to the committed mode.
    static_cast<SearchComboBox*>(compareCombo_)->setPreview(
        [this](const QString& mode) { canvas_->setCompareMode(mode); });

    // View cluster in the browser's order: ☑ Points · ☑ Lines · Compare · clear.
    // Points/Lines are real CHECKBOXES (persistent state, browser parity); the
    // menu actions stay the source of truth — these mirror them both ways.
    showPointsCheck_ = new QCheckBox("Points", this);
    showLinesCheck_ = new QCheckBox("Lines", this);
    const auto bindCheck = [this](QCheckBox* box, QAction* act) {
      box->setChecked(act->isChecked());
      box->setToolTip(act->toolTip().isEmpty() ? act->text() : act->toolTip());
      connect(box, &QCheckBox::toggled, this, [act](bool on) {
        if (act->isChecked() != on) act->setChecked(on);   // runs the action's own handler
      });
      connect(act, &QAction::toggled, this, [box](bool on) {
        QSignalBlocker blocked(box);   // echo back without re-entering the handler
        box->setChecked(on);
      });
    };
    bindCheck(showPointsCheck_, actShowPoints_);
    bindCheck(showLinesCheck_, actShowLines_);
    // Clear-lines is the trash at the END of the browser's View cluster, so it is built
    // here rather than passed as an action (makeToolSection puts actions first).
    auto* clearLinesBtn = new QToolButton(this);
    clearLinesBtn->setDefaultAction(actClearAll_);
    clearLinesBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    clearLinesBtn->setAutoRaise(true);
    clearLinesBtn->setIconSize(QSize(kToolIcon, kToolIcon));
    // Compare LEADS the section (user decision; browser twin: toolbar.js's View cluster),
    // on the row's own gap — the extra air it carried was for two bare words running
    // together. Captioned as the browser captions it, no colon.
    auto* compareLabel = new QLabel("Compare", this);
    compareLabel->setStyleSheet("padding-right: 2px;");
    addWrapped(row, makeToolSection("View", {}, {
        compareLabel, compareCombo_, showPointsCheck_, showLinesCheck_, clearLinesBtn }));
  }

  // "Image Size: W × H px" bar right above the canvas (browser parity: #image-info, a sibling
  // of .main-content rather than nested in .canvas-section). A real Qt::TopDockWidgetArea
  // dock, like selectedLineDock_ above it, so the row spans the full window width above both
  // dock corners instead of staying narrowed by the panel dock — this also retires
  // selectionPanel's setHeaderTopGap() hack, since both now sit below the same dock stack.
  void MainWindow::buildImageInfoBar() {
    auto* bar = new QWidget(this);
    bar->setObjectName("imageInfoBar");
    bar->setAttribute(Qt::WA_StyledBackground, true);
    auto* lay = new QHBoxLayout(bar);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(imageSizeInfo_);   // left-aligned label; the bar spans the window width
    lay->addStretch(1);
    imageInfoBar_ = bar;

    // The host carries the gaps around the styled bar so neither paints as part of its
    // background/border: no left/right margin (full-bleed, unlike the canvas column), an
    // adaptive top gap (0 while "Selected Line:" is shown, 8 otherwise — onSelectionChanged
    // keeps this in sync) and a 3px bottom gap, so the readout sits close
    // to the row it describes instead of floating in a black band above the canvas and the
    // assistant dock.
    imageInfoHost_ = new QWidget(this);
    imageInfoHost_->setObjectName("imageInfoHost");
    auto* hostLay = new QVBoxLayout(imageInfoHost_);
    hostLay->setContentsMargins(0, 8, 0, 3);
    hostLay->setSpacing(0);
    hostLay->addWidget(bar);

    imageInfoDock_ = new QDockWidget(this);
    imageInfoDock_->setObjectName("imageInfoDock");
    imageInfoDock_->setFeatures(QDockWidget::NoDockWidgetFeatures);
    imageInfoDock_->setTitleBarWidget(new QWidget(imageInfoDock_));   // no title bar of its own
    imageInfoDock_->setWidget(imageInfoHost_);
    addDockWidget(Qt::TopDockWidgetArea, imageInfoDock_);
    // Stacking below selectedLineDock_ (rather than Qt's default side-by-side tiling) needs
    // splitDockWidget, but selectedLineDock_ is still hidden here (nothing selected yet), and
    // splitting against a hidden dock doesn't register (same caveat as ensurePanelChatSplit).
    // onSelectionChanged re-affirms the split once selectedLineDock_ actually shows.
  }
}  // namespace stencil::gui

