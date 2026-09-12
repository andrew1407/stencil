#pragma once
class QAction;
class QLineEdit;
class QToolButton;
class QWidget;

namespace stencil::gui {

  // The header row's project-name group (browser topbar name field); widgets are built by the
  // toolbar.
  class ProjectNameBar {
   public:
    QLineEdit* field = nullptr;
    QWidget* group = nullptr;         // field + ✎/🎨/✓/✗ in one hover region
    QToolButton* edit = nullptr;      // ✎ rename affordance (enters edit mode)
    QToolButton* accept = nullptr;
    QToolButton* cancel = nullptr;
    QToolButton* colorBtn = nullptr;
    QToolButton* blankColorBtn = nullptr;   // recolour a blank project's background (blanks only)
    // QToolBar::addWidget wraps each button in a QWidgetAction; show/hide must toggle these
    // actions, or the toolbar ignores it.
    QAction* blankColorAction = nullptr;
    bool editing = false;   // the field is in edit mode
    bool hover = false;     // the cursor is over the field / ✎ / 🎨 group

    // ✓/✗ only in edit mode, ✎/🎨 only out of it; the pair keeps its slots when not hovered
    // (browser `visibility: hidden`).
    struct Chips {
      bool marks = false;           // ✓/✗ revealed
      bool affordances = false;     // ✎/🎨 occupy their slots
      bool affordancesPainted = false;   // …and are actually inked
      bool blankSwatch = false;
    };
    static Chips chipsFor(bool editable, bool editing, bool hover, bool blank) {
      Chips c;
      c.marks = editing;
      c.affordances = editable && !editing;
      c.affordancesPainted = c.affordances && hover;
      c.blankSwatch = blank && !editing;
      return c;
    }
    Chips chips(bool editable, bool blank) const {
      return chipsFor(editable, editing, hover, blank);
    }
  };

}  // namespace stencil::gui
