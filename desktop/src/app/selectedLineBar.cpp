#include "selectedLineBar.hpp"
#include "../support/flowLayout.hpp"
#include "../support/guiHelpers.hpp"
#include "../support/iconSet.hpp"
#include "../support/modalReveal.hpp"
#include "../support/numericInput.hpp"
#include "../support/searchCombo.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>

namespace stencil::gui {

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
    // twice what the browser's own #selection-panel -> #image-info gap does (user report).
    outer->setContentsMargins(12, 8, 12, 5);
    outer->setSpacing(0);

    card_ = new QWidget(this);
    card_->setObjectName("selectedLineCard");
    card_->setAttribute(Qt::WA_StyledBackground, true);
    outer->addWidget(card_);

    // FlowLayout, not QHBoxLayout: at a narrow window width the browser's own row
    // (flex; flex-wrap: wrap) wraps onto more lines rather than clip or overflow —
    // this wraps the same way, and its own height grows to fit however many it takes.
    // Margins match the browser's #selection-panel padding (10px 15px).
    auto* flow = new FlowLayout(card_, 0, 14, 8);
    flow->setContentsMargins(15, 10, 15, 10);

    auto* label = new QLabel("✎ Selected Line:", card_);
    label->setObjectName("selectedLineLabel");
    flow->addWidget(label);

    // Each field is its OWN label+control pair, wrapped as one unit — so the flow
    // never splits a label onto one line and its control onto the next.
    auto addField = [&](const QString& text, QWidget* control) {
      auto* group = new QWidget(card_);
      auto* pair = new QHBoxLayout(group);
      pair->setContentsMargins(0, 0, 0, 0);
      pair->setSpacing(6);
      auto* lbl = new QLabel(text, group);
      lbl->setObjectName("selectedLineFieldLabel");
      pair->addWidget(lbl);
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

    // selFillGroup — locked-area fill, hidden unless line.locked
    // (selectionPanel.js:29-33; drawingApp.js:1550-1560).
    fillGroup_ = new QWidget(card_);
    auto* fillRow = new QHBoxLayout(fillGroup_);
    fillRow->setContentsMargins(0, 0, 0, 0);
    fillRow->setSpacing(4);
    fillEnabled_ = new QCheckBox(fillGroup_);  // selFillEnabled
    fillSwatch_ = new QPushButton(fillGroup_);  // selFill
    setColorSwatch(fillSwatch_, currentFill_);
    fillClear_ = new QPushButton(fillGroup_);  // selFillClear (x icon)
    fillClear_->setObjectName("selectedLineFillClear");
    fillRow->addWidget(fillEnabled_);
    fillRow->addWidget(fillSwatch_);
    fillRow->addWidget(fillClear_);
    fillField_ = addField("Fill:", fillGroup_);

    // selDeselect (drawingApp.js:195 deselectLine) — the bar's own amber-accented CTA,
    // browser parity (.deselect-btn): no "Delete Line" here, that stays a global action
    // (Alt+Delete / the Lines tab's own row 🗑), matching the browser bar exactly.
    deselectBtn_ = new QPushButton("Deselect", card_);
    deselectBtn_->setObjectName("selectedLineDeselect");
    flow->addWidget(deselectBtn_);

    // ── wiring — each lambda early-returns while showLine is repopulating the
    // controls (updating_), matching the browser which guards via selectedLineIdx. ──
    connect(colorSwatch_, &QPushButton::clicked, this, [this] {
      if (updating_) return;
      const QColor c = support::pickColorAnimated(currentColor_, this, "Line color", colorSwatch_);
      if (!c.isValid()) return;
      currentColor_ = c;
      setColorSwatch(colorSwatch_, c);
      emit lineColorChanged(c.name());
    });
    connect(pointColorSwatch_, &QPushButton::clicked, this, [this] {
      if (updating_) return;
      const QColor c = support::pickColorAnimated(currentPointColor_, this, "Point color",
                                                   pointColorSwatch_);
      if (!c.isValid()) return;
      currentPointColor_ = c;
      setColorSwatch(pointColorSwatch_, c);
      emit linePointColorChanged(c.name());
    });
    connect(thickness_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
      if (!updating_) emit lineThicknessChanged(v);
    });
    connect(pointSize_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
      if (!updating_) emit linePointSizeChanged(v);
    });
    connect(style_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
      if (!updating_) emit lineStyleChanged(style_->currentData().toString());
    });
    connect(fillEnabled_, &QCheckBox::toggled, this, [this](bool on) {
      if (updating_) return;
      emit lineFillChanged(on ? currentFill_.name() : QStringLiteral("transparent"));
    });
    connect(fillSwatch_, &QPushButton::clicked, this, [this] {
      if (updating_) return;
      const QColor c = support::pickColorAnimated(currentFill_, this, "Area fill color", fillSwatch_);
      if (!c.isValid()) return;
      currentFill_ = c;
      setColorSwatch(fillSwatch_, c);
      {
        QSignalBlocker block(fillEnabled_);
        fillEnabled_->setChecked(true);
      }
      emit lineFillChanged(c.name());
    });
    connect(fillClear_, &QPushButton::clicked, this, [this] {
      if (updating_) return;
      {
        QSignalBlocker block(fillEnabled_);
        fillEnabled_->setChecked(false);
      }
      emit lineFillChanged(QStringLiteral("transparent"));
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

  void SelectedLineBar::restyleIcons(const QColor& iconColor) {
    if (fillClear_) fillClear_->setIcon(themedIcon("x", iconColor, 12));
    if (deselectBtn_) deselectBtn_->setIcon(themedIcon("x", QColor("#ffffff"), 13));
  }

  void SelectedLineBar::setDefaultFillColor(const QColor& color) { defaultFill_ = color; }

  void SelectedLineBar::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    // QDockWidget's own internal layout doesn't support a height-for-width content
    // widget properly: it reserves a height computed from an early/narrow width guess
    // and never re-asks once it settles on this bar's real (full-window) width, leaving
    // a huge empty amber gap under a single already-fitting row. Re-assert the REAL
    // height for the width we actually got, every time it changes.
    const int wantHeight = heightForWidth(width());
    if (wantHeight > 0 && wantHeight != height()) setFixedHeight(wantHeight);
  }

  void SelectedLineBar::showLine(const core::Line* line) {
    // No setVisible() here: MainWindow shows/hides the whole row via its dock
    // (selectedLineDock_); this only repopulates the controls.
    updating_ = true;
    if (line) {
      currentColor_ = QColor(QString::fromStdString(line->color));
      setColorSwatch(colorSwatch_, currentColor_);
      // A line with no point colour of its own shows the colour it actually draws in
      // (its stroke), via core::pointColorOr — not a blank or stale swatch.
      currentPointColor_ = QColor(QString::fromStdString(core::pointColorOr(*line)));
      setColorSwatch(pointColorSwatch_, currentPointColor_);
      thickness_->setValue(static_cast<int>(std::lround(line->thickness)));
      pointSize_->setValue(static_cast<int>(std::lround(line->pointSize)));
      const int sidx = style_->findData(QString::fromStdString(line->style));
      style_->setCurrentIndex(sidx >= 0 ? sidx : 0);

      // Fill controls only for locked areas (drawingApp.js:1551-1560) — the WHOLE field
      // (its "Fill:" label too), matching browser's #sel-fill-group display:none, not
      // just the inner checkbox/swatch/clear row (which left a dangling bare label).
      fillField_->setVisible(line->locked);
      if (line->locked) {
        const QString fc = QString::fromStdString(line->fillColor);
        const bool hasFill = !fc.isEmpty() && fc != "transparent";
        fillEnabled_->setChecked(hasFill);
        // browser layout.js fillState: enabled -> the line's own color; else the
        // Settings default, so the swatch always shows what ticking Fill would use.
        currentFill_ = hasFill ? QColor(fc) : defaultFill_;
        setColorSwatch(fillSwatch_, currentFill_);
      }
    }
    updating_ = false;
  }

}  // namespace stencil::gui
