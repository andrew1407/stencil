#include "../../support/control/dblReset.hpp"
#include "SelectedLineBar.hpp"

#include "cssColor.hpp"
#include "../../support/control/FlowLayout.hpp"
#include "../../support/control/lineLimits.hpp"
#include "../../support/guiHelpers.hpp"
#include "../../support/modal/modalReveal.hpp"
#include "../../support/control/numericInput.hpp"
#include "../../support/menu/SearchCombo.hpp"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QPushButton>
#include <QSize>
#include <QSpinBox>
#include <QVBoxLayout>

namespace stencil::gui {

  // One 46x24 colour well across every surface (browser css/layout/infoLine.css input[type="color"]).

  SelectedLineBar::SelectedLineBar(QWidget* parent) : QWidget(parent) {
    setObjectName("selectedLineBar");

    // The dock stretches this widget full-width; a separate card child carries the
    // border/radius/background, inset by the margins (browser #selection-panel).
    auto* outer = new QVBoxLayout(this);
    // Bottom inset 5, or the gap to the Image Size bar doubles the browser's.
    outer->setContentsMargins(12, 8, 12, 5);
    outer->setSpacing(0);

    card = new QWidget(this);
    card->setObjectName("selectedLineCard");
    card->setAttribute(Qt::WA_StyledBackground, true);
    outer->addWidget(card);

    // FlowLayout: the browser's row wraps. Margins are #selection-panel's padding (10px 15px),
    // spacings its gaps (12px).
    auto* flow = new FlowLayout(card, 0, 12, 12);
    flow->setContentsMargins(15, 10, 15, 10);

    // The browser's .sel-sep, in the same three places.
    auto addSeparator = [&]() -> QFrame* {
      auto* sep = new QFrame(card);
      sep->setObjectName("selectedLineSep");
      sep->setFrameShape(QFrame::NoFrame);
      sep->setFixedWidth(1);
      sep->setMinimumHeight(26);
      flow->addWidget(sep);
      return sep;
    };

    auto* label = new QLabel("✎ Selected Line:", card);
    label->setObjectName("selectedLineLabel");
    flow->addWidget(label);
    addSeparator();

    // Each label+control pair wraps as one unit.
    auto addField = [&](const QString& text, QWidget* control) {
      auto* group = new QWidget(card);
      auto* pair = new QHBoxLayout(group);
      pair->setContentsMargins(0, 0, 0, 0);
      pair->setSpacing(8);   // .control-group gap
      if (!text.isEmpty()) {
        auto* lbl = new QLabel(text, group);
        lbl->setObjectName("selectedLineFieldLabel");
        pair->addWidget(lbl);
      }
      pair->addWidget(control);
      flow->addWidget(group);
      return group;
    };

    // selColor — drawingApp.js:1545 / :181
    // A well and its 0-255 box, one control (browser .control-group: input[type=color] + .alpha-input).
    const auto withAlpha = [&](QPushButton* well, QSpinBox* box) {
      auto* pair = new QWidget(card);
      auto* h = new QHBoxLayout(pair);
      h->setContentsMargins(0, 0, 0, 0);
      h->setSpacing(8);
      well->setParent(pair);
      box->setParent(pair);
      h->addWidget(well);
      h->addWidget(box);
      return pair;
    };
    colorSwatch = new QPushButton(card);
    setColorSwatch(colorSwatch, currentColor);
    lineAlpha = alphaBox(colorSwatch, currentColor, "Line", [this](const QString& v) { emit lineColorChanged(v); });
    addField("Line Color:", withAlpha(colorSwatch, lineAlpha));

    pointColorSwatch = new QPushButton(card);
    setColorSwatch(pointColorSwatch, currentPointColor);
    pointAlpha = alphaBox(pointColorSwatch, currentPointColor, "Point",
                          [this](const QString& v) { emit linePointColorChanged(v); });
    addField("Point Color:", withAlpha(pointColorSwatch, pointAlpha));

    // selThickness — drawingApp.js:1546 / :182 (LIMITS.thickMin / thickMax)
    addSeparator();   // …and one after the colour wells, before the geometry fields
    const support::lineLimits::Table& limits = support::lineLimits::table();
    auto* thicknessSpin = new ExprSpinBox(card);
    thicknessSpin->setRange(limits.thickMin, limits.thickMax);
    thicknessSpin->setFixedWidth(60);
    thickness = thicknessSpin;
    addField("Thickness:", thickness);

    // selPointSize — drawingApp.js:1547 / :183 (LIMITS.pointMin / pointMax)
    auto* pointSizeSpin = new ExprSpinBox(card);
    pointSizeSpin->setRange(limits.pointMin, limits.pointMax);
    pointSizeSpin->setFixedWidth(60);
    pointSize = pointSizeSpin;
    addField("Point Size:", pointSize);

    // selStyle — drawingApp.js:1548 / :184
    auto* styleCombo = new SearchComboBox(card, /*searchable=*/false);
    styleCombo->addItem("Solid", "solid");
    styleCombo->addItem("Dashed", "dashed");
    styleCombo->addItem("Dotted", "dotted");
    style = styleCombo;
    support::setResetDefault(style, QStringLiteral("solid"));
    addField("Style:", style);

    fillSep = addSeparator();   // hidden with the group it introduces (see showLine)
    // selFillGroup — hidden unless line.locked (selectionPanel.js:29-33).
    fillGroup = new QWidget(card);
    auto* fillRow = new QHBoxLayout(fillGroup);
    fillRow->setContentsMargins(0, 0, 0, 0);
    fillRow->setSpacing(8);   // the same .control-group gap as every other field
    fillSwatch = new QPushButton(fillGroup);  // selFill
    fillSwatch->setObjectName("selectedLineFillSwatch");
    setColorSwatch(fillSwatch, currentFill);
    fillClear = new QPushButton(fillGroup);  // selFillClear (x icon)
    fillClear->setObjectName("selectedLineFillClear");
    // Browser's #sel-fill-clear title; one line, because a second renders as a bullet of one.
    fillClear->setToolTip("Clear fill — make the area transparent again");
    // A button's icon box defaults to 16 and scales an 11px pixmap back up — pin the box too.
    fillClear->setIconSize(QSize(13, 13));
    // selUnchain shows exactly when a line is closed (browser selectionPanel.js #sel-unchain).
    unchainBtn = new QPushButton("Unchain", fillGroup);
    unchainBtn->setObjectName("selectedLineUnchain");
    unchainBtn->setIconSize(QSize(13, 13));
    unchainBtn->setToolTip("Unchain area\nBreak the closed shape back into an open line.\n"
                            "Alt+Ctrl+drag on the line does the same, at the spot you pull.");
    // No on/off tick: the fill is a colour with an alpha, 0 means "none" (browser applyFill).
    auto* fillWord = new QLabel("Fill:", fillGroup);
    fillWord->setObjectName("selectedLineFieldLabel");
    fillRow->addWidget(fillWord);
    fillRow->addWidget(fillSwatch);
    fillAlpha = alphaBox(fillSwatch, currentFill, "Fill", [this](const QString& v) { emit lineFillChanged(v); });
    fillRow->addWidget(fillAlpha);
    fillRow->addWidget(fillClear);
    fillRow->addWidget(unchainBtn);
    fillField = addField(QString(), fillGroup);

    // selDeselect (browser .deselect-btn); Delete Line stays a global action, as in the browser.
    addSeparator();
    deselectBtn = new QPushButton("Deselect", card);
    deselectBtn->setObjectName("selectedLineDeselect");
    flow->addWidget(deselectBtn);

    wireColorWell(colorSwatch, currentColor, "Line color",
                  [this](const QString& v, bool preview) { emit lineColorChanged(v, preview); },
                  [this] { return lineColorDefault ? lineColorDefault() : defaultVisuals::table().color; });
    wireColorWell(pointColorSwatch, currentPointColor, "Point color",
                  [this](const QString& v, bool preview) { emit linePointColorChanged(v, preview); });
    connect(thickness, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
      if (!updating) emit lineThicknessChanged(v);
    });
    connect(pointSize, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
      if (!updating) emit linePointSizeChanged(v);
    });
    connect(style, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
      if (!updating) emit lineStyleChanged(style->currentData().toString());
    });
    // Picking a colour on an unfilled area also lifts it off zero alpha (browser controlsBinder:
    // the nudge to 255).
    wireColorWell(fillSwatch, currentFill, "Area fill color",
                  [this](const QString& v, bool preview) {
                    if (!preview && currentFill.alpha() == 0) {
                      currentFill.setAlpha(255);
                      setColorSwatch(fillSwatch, currentFill);
                      emit lineFillChanged(cssName(currentFill));
                      return;
                    }
                    emit lineFillChanged(v, preview);
                  });
    connect(fillClear, &QPushButton::clicked, this, [this] {
      if (updating) return;
      emit lineFillChanged(QStringLiteral("transparent"));
    });
    connect(unchainBtn, &QPushButton::clicked, this, [this] {
      if (!updating) emit unchainRequested();
    });
    connect(deselectBtn, &QPushButton::clicked, this, [this] {
      if (!updating) emit deselectRequested();
    });

    // Qt polishes lazily, and the hidden card's FlowLayout would centre rows against pre-QSS
    // sizeHints.
    for (QWidget* w : card->findChildren<QWidget*>()) w->ensurePolished();

    restyleIcons(QColor("#ffffff"));  // fillClear icon; deselect gets its own fixed white
    showLine(nullptr);
  }
}  // namespace stencil::gui

