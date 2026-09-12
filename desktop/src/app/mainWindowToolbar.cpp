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

  // ONE wrapping run, in the browser's order (toolbar.js): Image · Description · Projects ·
  // Connections · Edit · Line · Point · Draw · View · Zoom · Page · Formula · Data ·
  // Settings. Like the browser's single flex-wrap container, it re-packs with the window —
  // so the four sub-builders all append to the same row and MUST run in this order.
  void MainWindow::buildToolbar() {
    buildMainToolbar();
    buildStyleToolbar();
    buildDrawViewToolbar();
    buildPageFormulaToolbar();
    buildImageInfoBar();
    // Shared hover shimmer on every interactive control across the toolbar rows (buttons, combos,
    // spinboxes, the f(x,y) checkbox, text fields) so the whole toolbar has one consistent hover
    // treatment — not just the makeToolSection icons.
    for (QToolBar* tb : findChildren<QToolBar*>()) {
      for (QToolButton* b : tb->findChildren<QToolButton*>())
        if (b != logoBtn_) installHoverShimmer(b);   // skip the logo (its own art/affordance)
      for (QComboBox* c : tb->findChildren<QComboBox*>()) installHoverShimmer(c);
      for (QAbstractSpinBox* s : tb->findChildren<QAbstractSpinBox*>()) installHoverShimmer(s);
      for (QCheckBox* c : tb->findChildren<QCheckBox*>()) installHoverShimmer(c);   // f(x,y) pill
      for (QLineEdit* le : tb->findChildren<QLineEdit*>())
        if (le != nameBar_.field) installHoverShimmer(le);   // skip the rename field
    }
    // Hand cursor on everything clickable, like the browser's `cursor: pointer` buttons.
    // Combos and text fields keep Qt's arrow / I-beam, which is what the browser shows too.
    for (QToolBar* tb : findChildren<QToolBar*>())
      for (QAbstractButton* b : tb->findChildren<QAbstractButton*>())
        if (!b->testAttribute(Qt::WA_SetCursor))
          b->setCursor(b->isEnabled() ? Qt::PointingHandCursor : Qt::ForbiddenCursor);
    // The buttons only exist now, so the destructive ones get their filled-red face here
    // (styleActionIcons already ran, with nothing to find).
    styleDangerToolButtons();
  }

  // The f(x,y) transform fields (browser #formula-inputs, toolbar.js): two bare
  // monospace fields (each placeholder already reads "x(x)=" / "y(y)="), living
  // inside the Formula section so they share the pill's row height and centring.
  namespace {
    // The browser's fields are a flat 180px (toolbar.js `style="width:180px"`). Qt has no
    // "preferred width", and the FORMULA section is content-sized — it sits mid-row, so it
    // cannot take slack the way an end-of-row cluster can — which left the fields at the
    // ~158px a QLineEdit asks for. The hint IS the width here, so it says 180; the minimum
    // below still lets the row squeeze them when it has to.
    class FormulaField : public QLineEdit {
     public:
      explicit FormulaField(QWidget* parent) : QLineEdit(parent) {}
      QSize sizeHint() const override {
        return QSize(kFormulaFieldW, QLineEdit::sizeHint().height());
      }
    };
  }  // namespace

  void MainWindow::buildFormulaFields() {
    formulaGroup_ = new QWidget(this);
    // Maximum, not Expanding: the pair is exactly as wide as the two fields want (2 × 180 +
    // the gap) and may only SHRINK from there. Expanding made this cluster swallow the row's
    // leftover width — a long empty stretch after the fields, with DATA and SETTINGS shoved
    // to the far edge — and, once the fields hid, left the lone pill floating in the middle
    // of that empty box instead of sitting under its caption.
    formulaGroup_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    auto* fl = new QHBoxLayout(formulaGroup_);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->setSpacing(6);   // the browser's gap between the two fields
    const auto makeField = [this](const char* placeholder, const char* tip) {
      auto* e = new FormulaField(formulaGroup_);
      e->setPlaceholderText(placeholder);
      e->setToolTip(tip);
      // Monospace, like the browser's — a formula is code, and the digits have to line up.
      QFont f = e->font();
      f.setFamily(QFontDatabase::systemFont(QFontDatabase::FixedFont).family());
      e->setFont(f);
      // Elastic between the browser's width and the floor: the row hands its slack to the
      // pair (the FORMULA cluster closes that row), so they read like the browser's on a
      // normal window and give the space back on a narrow one instead of overflowing.
      e->setMinimumWidth(kFormulaFieldMinW);
      e->setMaximumWidth(kFormulaFieldW);
      e->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
      return e;
    };
    formulaX_ = makeField("x(x)=", "Transform formula for x — e.g. x*2 + 1 (empty = identity)");
    formulaY_ = makeField("y(y)=", "Transform formula for y — e.g. y/2 (empty = identity)");
    // Icon-only, like the browser's #formula-error (its alert glyph with a title) — the
    // tooltip carries the words. Named so tests and restyling can find it without matching
    // on its text, and kept in the danger colour it has always had.
    formulaError_ = new QLabel("\u26A0", formulaGroup_);
    formulaError_->setObjectName("formulaError");
    formulaError_->setToolTip("Invalid formula");
    formulaError_->setStyleSheet("color:#d9534f;");
    formulaError_->setVisible(false);
    // The fields carry the stretch and the tail carries none, so the group's growth goes
    // into the pair until they reach the browser's width and only the excess lands in the
    // tail. With no tail at all that excess came out as SPACING and the pair drifted apart.
    fl->addWidget(formulaX_, 1);
    fl->addWidget(formulaY_, 1);
    fl->addWidget(formulaError_);
    fl->addStretch(0);
  }

  QToolBar* MainWindow::toolRow() const { return findChild<QToolBar*>("mainToolbar"); }

  // The header row (always visible) and then the one wrapping tool row below it. The
  // addToolBar/addToolBarBreak order between them is what fixes the visual row order, so
  // the two calls may not swap; tests/mainWindow.composition.gui.cpp pins the result.
  void MainWindow::buildMainToolbar() {
    buildHeaderRow();
    buildToolSectionsRow();
  }

}  // namespace stencil::gui

