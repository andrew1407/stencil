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

// MainWindow's toolbar assembly: the Draw · View sections and the Image Size bar.

namespace stencil::gui {

  void MainWindow::buildDrawViewToolbar() {
    QToolBar* row = toolRow();
    addWrappedSeparator(row);
    addWrapped(row, makeToolSection("Draw", {actStartDraw}, {drawModeBtn}));
    addWrappedSeparator(row);
    // Kept in sync with the View → Compare submenu radio set.
    compareCombo = new SearchComboBox(this, /*searchable=*/false);
    // Short labels: the closed combo is sized by its widest item; the tooltip spells each out.
    compareCombo->addItem("None", "none");
    compareCombo->addItem("Original", "original");
    compareCombo->addItem(QString::fromUtf8("Split ↔"), "vertical");
    compareCombo->addItem(QString::fromUtf8("Split ↕"), "horizontal");
    // The browser's words (toolbar.js #compare-mode); the cycle chord is the trailing "(…)"
    // tipContent draws as the keycap, so not named in the prose.
    setTipBase(compareCombo,
               "Compare with original\n"
               "• None — normal editing\n"
               "• Original — the original only (crop + rotation)\n"
               "• Vertical split — original left, edit right\n"
               "• Horizontal split — original top, edit bottom\n"
               "(hold Alt+Shift+O to peek)");
    setTipHotkey(compareCombo, actCycleCompare);
    setTipReason(compareCombo, "Load an image to compare");
    // Wired here, not in buildStyleToolbar, which runs before this one: a connect() on a null
    // compareCombo is silently dropped.
    connect(compareCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
              setCompareModeUi(compareCombo->currentData().toString());
            });
    // Hover-preview repaints only; leaving without a pick reverts to the committed mode.
    static_cast<SearchComboBox*>(compareCombo)->setPreview(
        [this](const QString& mode) { canvas->setCompareMode(mode); });

    // Points/Lines are real checkboxes (browser parity); the menu actions stay the source of
    // truth.
    showPointsCheck = new QCheckBox("Points", this);
    showLinesCheck = new QCheckBox("Lines", this);
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
    bindCheck(showPointsCheck, actShowPoints);
    bindCheck(showLinesCheck, actShowLines);
    // Built here so it lands at the end of the cluster (makeToolSection puts actions first).
    auto* clearLinesBtn = new QToolButton(this);
    clearLinesBtn->setDefaultAction(actClearAll);
    clearLinesBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    clearLinesBtn->setAutoRaise(true);
    clearLinesBtn->setIconSize(QSize(TOOL_ICON, TOOL_ICON));
    // Compare leads (browser twin: toolbar.js View cluster), captioned with no colon.
    auto* compareLabel = new QLabel("Compare", this);
    compareLabel->setStyleSheet("padding-right: 2px;");
    addWrapped(row, makeToolSection("View", {}, {
        compareLabel, compareCombo, showPointsCheck, showLinesCheck, clearLinesBtn }));
  }

  // browser #image-info. A real top dock like selectedLineDock, so it spans the full window width
  // above both dock corners.
  void MainWindow::buildImageInfoBar() {
    auto* bar = new QWidget(this);
    bar->setObjectName("imageInfoBar");
    bar->setAttribute(Qt::WA_StyledBackground, true);
    auto* lay = new QHBoxLayout(bar);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(imageSizeInfo);   // left-aligned label; the bar spans the window width
    lay->addStretch(1);
    imageInfoBar = bar;

    // The host carries the gaps: no side margin, a top gap of 0 while "Selected Line:" shows and 8
    // otherwise (onSelectionChanged), 3px below.
    imageInfoHost = new QWidget(this);
    imageInfoHost->setObjectName("imageInfoHost");
    auto* hostLay = new QVBoxLayout(imageInfoHost);
    hostLay->setContentsMargins(0, 8, 0, 3);
    hostLay->setSpacing(0);
    hostLay->addWidget(bar);

    imageInfoDock = new QDockWidget(this);
    imageInfoDock->setObjectName("imageInfoDock");
    imageInfoDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    imageInfoDock->setTitleBarWidget(new QWidget(imageInfoDock));   // no title bar of its own
    imageInfoDock->setWidget(imageInfoHost);
    addDockWidget(Qt::TopDockWidgetArea, imageInfoDock);
    // splitDockWidget against the still-hidden selectedLineDock does not register;
    // onSelectionChanged re-affirms it once shown.
  }
}  // namespace stencil::gui

