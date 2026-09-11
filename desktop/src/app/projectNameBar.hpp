#pragma once
class QAction;
class QLineEdit;
class QToolButton;
class QWidget;

namespace stencil::gui {

  // The header row's project-name group: the field, its inline-rename ✓/✗ marks, the
  // ✎/🎨 hover affordances and the blank-fill swatch. Mirrors the browser topbar name
  // field. The widgets are built by the toolbar and parented to it; this holds them
  // together with the state that decides which of them belong in the row right now.
  class ProjectNameBar {
   public:
    QLineEdit* field = nullptr;
    QWidget* group = nullptr;         // field + ✎/🎨/✓/✗ in one hover region
    QToolButton* edit = nullptr;      // ✎ rename affordance (enters edit mode)
    QToolButton* accept = nullptr;
    QToolButton* cancel = nullptr;
    // Per-project accent swatch next to the field (browser's color control): its popup
    // chooses a custom name colour or reverts to the theme accent.
    QToolButton* colorBtn = nullptr;
    QToolButton* blankColorBtn = nullptr;   // recolour a blank project's background (blanks only)
    // QToolBar::addWidget wraps each button in a QWidgetAction; show/hide must toggle
    // THESE actions, not just the widgets, or the toolbar ignores it.
    QAction* blankColorAction = nullptr;
    bool editing = false;   // the field is in edit mode
    bool hover = false;     // the cursor is over the field / ✎ / 🎨 group

    // Which chips belong in the row. Browser-like: ✓/✗ show only IN edit mode, the ✎/🎨
    // pair only out of it; the pair keeps its slots and is merely painted out when the
    // group is not hovered (the browser's `visibility: hidden`) so nothing shifts.
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
