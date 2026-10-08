#include "MainWindow.hpp"
#include "ToolbarBuilder.hpp"
#include "mainWindowHelpers.hpp"
#include "../../support/motion/ShimmerOverlay.hpp"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolBar>

// MainWindow's toolbar assembly: the wrapping run, the formula fields and the row order.

namespace stencil::gui {

  // One wrapping run in the browser's order (toolbar.js); the sub-builders append to the same row
  // and must run in this order.
  void ToolbarBuilder::buildToolbar() {
    buildMainToolbar();
    buildStyleToolbar();
    buildDrawViewToolbar();
    buildPageFormulaToolbar();
    buildImageInfoBar();
    // One hover treatment across every toolbar control, not just the makeToolSection icons.
    for (QToolBar* tb : w.findChildren<QToolBar*>()) {
      for (QToolButton* b : tb->findChildren<QToolButton*>())
        if (b != w.tools.logoBtn) installHoverShimmer(b);   // skip the logo (its own art/affordance)
      for (QComboBox* c : tb->findChildren<QComboBox*>()) installHoverShimmer(c);
      for (QAbstractSpinBox* s : tb->findChildren<QAbstractSpinBox*>()) installHoverShimmer(s);
      for (QCheckBox* c : tb->findChildren<QCheckBox*>()) installHoverShimmer(c);   // f(x,y) pill
      for (QLineEdit* le : tb->findChildren<QLineEdit*>())
        if (le != w.nameBar.field) installHoverShimmer(le);   // skip the rename field
    }
    // Hand cursor on everything clickable (browser `cursor: pointer`); combos and fields keep
    // Qt's.
    for (QToolBar* tb : w.findChildren<QToolBar*>())
      for (QAbstractButton* b : tb->findChildren<QAbstractButton*>())
        if (!b->testAttribute(Qt::WA_SetCursor))
          b->setCursor(b->isEnabled() ? Qt::PointingHandCursor : Qt::ForbiddenCursor);
    // The buttons only exist now; styleActionIcons already ran with nothing to find.
    w.parts.theme.styleDangerToolButtons();
    w.parts.theme.installThemeLens();
    buildIconDrags();
  }

  // The browser's #formula-inputs (toolbar.js), inside the Formula section so they share the
  // pill's row.
  namespace {
    // The browser's fields are 180px; Qt has no preferred width and the mid-row section cannot
    // take slack, so the hint is the width.
    class FormulaField : public QLineEdit {
     public:
      explicit FormulaField(QWidget* parent) : QLineEdit(parent) {}
      QSize sizeHint() const override {
        return QSize(FORMULA_FIELD_W, QLineEdit::sizeHint().height());
      }
    };
  }  // namespace

  void ToolbarBuilder::buildFormulaFields() {
    w.tools.formulaGroup = new QWidget(&w);
    // Maximum, not Expanding: Expanding swallowed the row's leftover and left the lone pill
    // floating mid-box once the fields hid.
    w.tools.formulaGroup->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    auto* fl = new QHBoxLayout(w.tools.formulaGroup);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->setSpacing(6);   // the browser's gap between the two fields
    const auto makeField = [this](const char* placeholder, const char* tip) {
      auto* e = new FormulaField(w.tools.formulaGroup);
      e->setPlaceholderText(placeholder);
      e->setToolTip(tip);
      QFont f = e->font();
      f.setFamily(QFontDatabase::systemFont(QFontDatabase::FixedFont).family());
      e->setFont(f);
      // Elastic between the browser's width and the floor, so a narrow window squeezes instead of
      // overflowing.
      e->setMinimumWidth(FORMULA_FIELD_MIN_W);
      e->setMaximumWidth(FORMULA_FIELD_W);
      e->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
      return e;
    };
    w.tools.formulaX = makeField("x(x)=", "Transform formula for x — e.g. x*2 + 1 (empty = identity)");
    w.tools.formulaY = makeField("y(y)=", "Transform formula for y — e.g. y/2 (empty = identity)");
    // Icon-only like the browser's #formula-error; named so tests find it without matching on
    // text.
    w.tools.formulaError = new QLabel("\u26A0", w.tools.formulaGroup);
    w.tools.formulaError->setObjectName("formulaError");
    w.tools.formulaError->setToolTip("Invalid formula");
    w.tools.formulaError->setStyleSheet("color:#d9534f;");
    w.tools.formulaError->setVisible(false);
    // The fields carry the stretch, the tail none: with no tail the excess became spacing and the
    // pair drifted apart.
    fl->addWidget(w.tools.formulaX, 1);
    fl->addWidget(w.tools.formulaY, 1);
    fl->addWidget(w.tools.formulaError);
    fl->addStretch(0);
  }

  QToolBar* ToolbarBuilder::toolRow() const { return w.findChild<QToolBar*>("mainToolbar"); }

  // The addToolBar/addToolBarBreak order fixes the visual row order;
  // tests/app/setup/MainWindow.composition.gui.cpp pins it.
  void ToolbarBuilder::buildMainToolbar() {
    buildHeaderRow();
    buildToolSectionsRow();
  }

}  // namespace stencil::gui

