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

  // ONE colour well across every surface: the 46x24 this app's toolbar already used, which
  // the browser and extension now match too (browser css/layout.css input[type="color"]).
  // A well as tall as the text fields beside it made the colour the loudest thing in the row.

  SelectedLineBar::SelectedLineBar(QWidget* parent) : QWidget(parent) {
    setObjectName("selectedLineBar");

    // The dock stretches THIS widget to the window's full width, but the amber box
    // itself must sit inset from all four edges with its border fully visible on
    // every side — browser #selection-panel parity, not the flush/border-bottom-only
    // treatment the other toolbar-style bars use. So this outer widget stays a plain,
    // unstyled strip; a separate "card" child carries the border/radius/background and
    // is what the outer margins below inset.
    auto* outer = new QVBoxLayout(this);
    // Bottom inset halved (10 -> 5): the gap down to the Image Size bar below it read as
    // twice what the browser's own #selection-panel -> #image-info gap does.
    outer->setContentsMargins(12, 8, 12, 5);
    outer->setSpacing(0);

    card_ = new QWidget(this);
    card_->setObjectName("selectedLineCard");
    card_->setAttribute(Qt::WA_StyledBackground, true);
    outer->addWidget(card_);

    // FlowLayout, not QHBoxLayout: the browser's row wraps rather than clip at a narrow
    // width, and this wraps the same way, growing its own height to fit. Margins match
    // #selection-panel's padding (10px 15px); spacings are its flex gaps (12px).
    auto* flow = new FlowLayout(card_, 0, 12, 12);
    flow->setContentsMargins(15, 10, 15, 10);

    // The bar's parts, told apart by a hairline in its own amber rather than by spacing
    // alone — the browser's .sel-sep, in the same three places.
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

    // Each field is its OWN label+control pair, wrapped as one unit — so the flow
    // never splits a label onto one line and its control onto the next.
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

    // selPointColor — the point colour, set independently of the stroke.
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
    // selFillGroup — locked-area fill, hidden unless line.locked
    // (selectionPanel.js:29-33; drawingApp.js:1550-1560).
    fillGroup_ = new QWidget(card_);
    auto* fillRow = new QHBoxLayout(fillGroup_);
    fillRow->setContentsMargins(0, 0, 0, 0);
    fillRow->setSpacing(8);   // the same .control-group gap as every other field
    fillSwatch_ = new QPushButton(fillGroup_);  // selFill
    fillSwatch_->setObjectName("selectedLineFillSwatch");
    setColorSwatch(fillSwatch_, currentFill_);
    fillClear_ = new QPushButton(fillGroup_);  // selFillClear (x icon)
    fillClear_->setObjectName("selectedLineFillClear");
    // Same words as the browser's #sel-fill-clear title and the same small glyph
    // (restyleIcons). One line: the heading's "term — description" renders as a title plus
    // a muted subtitle, where a second line would come out as a bullet of one.
    fillClear_->setToolTip("Clear fill — make the area transparent again");
    // The pixmap is 11px, but a button's icon BOX defaults to the style's 16 and scales it
    // back up — the glyph stayed big however small the icon was drawn. Pin the box too.
    fillClear_->setIconSize(QSize(13, 13));
    // selUnchain — the way back OUT of an area, in the same group: it shows exactly when
    // a line is closed, which is exactly when unchaining means anything (browser
    // selectionPanel.js #sel-unchain).
    unchainBtn_ = new QPushButton("Unchain", fillGroup_);
    unchainBtn_->setObjectName("selectedLineUnchain");
    // Icon + label, like the browser's #sel-unchain (a `link` glyph beside the word) and
    // like this bar's own Deselect — the icon itself is themed in restyleIcons.
    unchainBtn_->setIconSize(QSize(13, 13));
    unchainBtn_->setToolTip("Unchain area\nBreak the closed shape back into an open line.\n"
                            "Alt+Ctrl+drag on the line does the same, at the spot you pull.");
    // No on/off tick: the fill IS a colour with an alpha, and 0 is what "none" means
    // (browser selectionPanel.js applyFill). Just the label, its well and the clear
    // button, so the label takes the colon every other field in this bar has.
    auto* fillWord = new QLabel("Fill:", fillGroup_);
    fillWord->setObjectName("selectedLineFieldLabel");
    fillRow->addWidget(fillWord);
    fillRow->addWidget(fillSwatch_);
    fillRow->addWidget(fillClear_);
    fillRow->addWidget(unchainBtn_);
    fillField_ = addField(QString(), fillGroup_);

    // selDeselect (drawingApp.js:195 deselectLine) — the bar's own amber-accented CTA,
    // browser parity (.deselect-btn): no "Delete Line" here, that stays a global action
    // (Alt+Delete / the Lines tab's own row 🗑), matching the browser bar exactly.
    addSeparator();
    deselectBtn_ = new QPushButton("Deselect", card_);
    deselectBtn_->setObjectName("selectedLineDeselect");
    flow->addWidget(deselectBtn_);

    // wiring — each lambda early-returns while showLine is repopulating the
    // controls (updating_), matching the browser which guards via selectedLineIdx.
    // Every colour well in the bar behaves the same: the line follows the picker as it is
    // dragged (Cancel is handed the original back by pickColorAnimated, so only the
    // accepted value lands here), and the swatch tracks it either way. `emit` is the one
    // difference — it takes the css value and whether this is still a preview.
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
    // …and the fill is the same well with one extra rule: picking a colour on an UNFILLED
    // area must also lift it off zero alpha, or the choice would apply invisibly (browser
    // controlsBinder: the same nudge to 255).
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

    // Polish up front: Qt polishes lazily (first show), so the still-hidden card's
    // FlowLayout would centre rows against pre-QSS sizeHints and never re-align.
    for (QWidget* w : card_->findChildren<QWidget*>()) w->ensurePolished();

    restyleIcons(QColor("#ffffff"));  // fillClear icon; deselect gets its own fixed white
    showLine(nullptr);
  }
}  // namespace stencil::gui

