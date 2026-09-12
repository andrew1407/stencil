#include "selectedLineBar.hpp"

#include "controlReveal.hpp"
#include "cssColor.hpp"
#include "../support/flowLayout.hpp"
#include "../support/guiHelpers.hpp"
#include "../support/iconSet.hpp"
#include "../support/modalReveal.hpp"
#include "../support/numericInput.hpp"
#include "../support/searchCombo.hpp"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QPushButton>
#include <QSize>
#include <QTimer>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>

namespace stencil::gui {

  // One 46x24 colour well across every surface (browser css/layout.css input[type="color"]).

  SelectedLineBar::SelectedLineBar(QWidget* parent) : QWidget(parent) {
    setObjectName("selectedLineBar");

    // The dock stretches this widget full-width; a separate card child carries the
    // border/radius/background, inset by the margins (browser #selection-panel).
    auto* outer = new QVBoxLayout(this);
    // Bottom inset 5, or the gap to the Image Size bar doubles the browser's.
    outer->setContentsMargins(12, 8, 12, 5);
    outer->setSpacing(0);

    card_ = new QWidget(this);
    card_->setObjectName("selectedLineCard");
    card_->setAttribute(Qt::WA_StyledBackground, true);
    outer->addWidget(card_);

    // FlowLayout: the browser's row wraps. Margins are #selection-panel's padding (10px 15px),
    // spacings its gaps (12px).
    auto* flow = new FlowLayout(card_, 0, 12, 12);
    flow->setContentsMargins(15, 10, 15, 10);

    // The browser's .sel-sep, in the same three places.
    auto addSeparator = [&]() -> QFrame* {
      auto* sep = new QFrame(card_);
      sep->setObjectName("selectedLineSep");
      sep->setFrameShape(QFrame::NoFrame);
      sep->setFixedWidth(1);
      sep->setMinimumHeight(26);
      flow->addWidget(sep);
      return sep;
    };

    auto* label = new QLabel("✎ Selected Line:", card_);
    label->setObjectName("selectedLineLabel");
    flow->addWidget(label);
    addSeparator();

    // Each label+control pair wraps as one unit.
    auto addField = [&](const QString& text, QWidget* control) {
      auto* group = new QWidget(card_);
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
    colorSwatch_ = new QPushButton(card_);
    setColorSwatch(colorSwatch_, currentColor_);
    addField("Line Color:", colorSwatch_);

    pointColorSwatch_ = new QPushButton(card_);
    setColorSwatch(pointColorSwatch_, currentPointColor_);
    addField("Point Color:", pointColorSwatch_);

    // selThickness — drawingApp.js:1546 / :182 (min 1, max 20)
    addSeparator();   // …and one after the colour wells, before the geometry fields
    auto* thicknessSpin = new ExprSpinBox(card_);
    thicknessSpin->setRange(1, 20);
    thicknessSpin->setFixedWidth(60);
    thickness_ = thicknessSpin;
    addField("Thickness:", thickness_);

    // selPointSize — drawingApp.js:1547 / :183 (min 1, max 30)
    auto* pointSizeSpin = new ExprSpinBox(card_);
    pointSizeSpin->setRange(1, 30);
    pointSizeSpin->setFixedWidth(60);
    pointSize_ = pointSizeSpin;
    addField("Point Size:", pointSize_);

    // selStyle — drawingApp.js:1548 / :184
    auto* styleCombo = new SearchComboBox(card_, /*searchable=*/false);
    styleCombo->addItem("Solid", "solid");
    styleCombo->addItem("Dashed", "dashed");
    styleCombo->addItem("Dotted", "dotted");
    style_ = styleCombo;
    addField("Style:", style_);

    fillSep_ = addSeparator();   // hidden with the group it introduces (see showLine)
    // selFillGroup — hidden unless line.locked (selectionPanel.js:29-33).
    fillGroup_ = new QWidget(card_);
    auto* fillRow = new QHBoxLayout(fillGroup_);
    fillRow->setContentsMargins(0, 0, 0, 0);
    fillRow->setSpacing(8);   // the same .control-group gap as every other field
    fillSwatch_ = new QPushButton(fillGroup_);  // selFill
    fillSwatch_->setObjectName("selectedLineFillSwatch");
    setColorSwatch(fillSwatch_, currentFill_);
    fillClear_ = new QPushButton(fillGroup_);  // selFillClear (x icon)
    fillClear_->setObjectName("selectedLineFillClear");
    // Browser's #sel-fill-clear title; one line, because a second renders as a bullet of one.
    fillClear_->setToolTip("Clear fill — make the area transparent again");
    // A button's icon box defaults to 16 and scales an 11px pixmap back up — pin the box too.
    fillClear_->setIconSize(QSize(13, 13));
    // selUnchain shows exactly when a line is closed (browser selectionPanel.js #sel-unchain).
    unchainBtn_ = new QPushButton("Unchain", fillGroup_);
    unchainBtn_->setObjectName("selectedLineUnchain");
    unchainBtn_->setIconSize(QSize(13, 13));
    unchainBtn_->setToolTip("Unchain area\nBreak the closed shape back into an open line.\n"
                            "Alt+Ctrl+drag on the line does the same, at the spot you pull.");
    // No on/off tick: the fill is a colour with an alpha, 0 means "none" (browser applyFill).
    auto* fillWord = new QLabel("Fill:", fillGroup_);
    fillWord->setObjectName("selectedLineFieldLabel");
    fillRow->addWidget(fillWord);
    fillRow->addWidget(fillSwatch_);
    fillRow->addWidget(fillClear_);
    fillRow->addWidget(unchainBtn_);
    fillField_ = addField(QString(), fillGroup_);

    // selDeselect (browser .deselect-btn); Delete Line stays a global action, as in the browser.
    addSeparator();
    deselectBtn_ = new QPushButton("Deselect", card_);
    deselectBtn_->setObjectName("selectedLineDeselect");
    flow->addWidget(deselectBtn_);

    // Each lambda early-returns while showLine repopulates (updating_), the browser's
    // selectedLineIdx guard.
    // The line follows the picker as it is dragged; Cancel hands the original back through
    // pickColorAnimated.
    const auto wireColorWell = [this](QPushButton* well, QColor& current, const char* title,
                                      std::function<void(const QString&, bool)> send) {
      connect(well, &QPushButton::clicked, this, [this, well, &current, title, send] {
        if (updating_) return;
        const QColor c = support::pickColorAnimated(
            current, this, title, well, QRect(),
            [well, send](const QColor& p) { setColorSwatch(well, p); send(cssName(p), true); },
            /*withAlpha=*/true);
        if (!c.isValid()) return;
        current = c;
        setColorSwatch(well, current);
        send(cssName(current), false);
      });
    };
    wireColorWell(colorSwatch_, currentColor_, "Line color",
                  [this](const QString& v, bool preview) { emit lineColorChanged(v, preview); });
    wireColorWell(pointColorSwatch_, currentPointColor_, "Point color",
                  [this](const QString& v, bool preview) { emit linePointColorChanged(v, preview); });
    connect(thickness_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
      if (!updating_) emit lineThicknessChanged(v);
    });
    connect(pointSize_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
      if (!updating_) emit linePointSizeChanged(v);
    });
    connect(style_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
      if (!updating_) emit lineStyleChanged(style_->currentData().toString());
    });
    // Picking a colour on an unfilled area also lifts it off zero alpha (browser controlsBinder:
    // the nudge to 255).
    wireColorWell(fillSwatch_, currentFill_, "Area fill color",
                  [this](const QString& v, bool preview) {
                    if (!preview && currentFill_.alpha() == 0) {
                      currentFill_.setAlpha(255);
                      setColorSwatch(fillSwatch_, currentFill_);
                      emit lineFillChanged(cssName(currentFill_));
                      return;
                    }
                    emit lineFillChanged(v, preview);
                  });
    connect(fillClear_, &QPushButton::clicked, this, [this] {
      if (updating_) return;
      emit lineFillChanged(QStringLiteral("transparent"));
    });
    connect(unchainBtn_, &QPushButton::clicked, this, [this] {
      if (!updating_) emit unchainRequested();
    });
    connect(deselectBtn_, &QPushButton::clicked, this, [this] {
      if (!updating_) emit deselectRequested();
    });

    // Qt polishes lazily, and the hidden card's FlowLayout would centre rows against pre-QSS
    // sizeHints.
    for (QWidget* w : card_->findChildren<QWidget*>()) w->ensurePolished();

    restyleIcons(QColor("#ffffff"));  // fillClear icon; deselect gets its own fixed white
    showLine(nullptr);
  }
}  // namespace stencil::gui

