#pragma once
#include <QHash>
#include <QPointer>
#include <QRect>
#include <QSet>
#include <QtGlobal>

class QAction;
class QDialog;
class QMenu;
class QObject;
class QTimer;
class QToolButton;
class QWidget;

namespace stencil::gui {

  // Modal popovers (support/popover.hpp): dblclick / right-click opens the dialog as a compact
  // popover; a plain click is deferred one double-click interval so exec() cannot swallow the
  // second click.
  class PopoverHost {
   public:
    QSet<QAction*> dialogActions;         // the actions whose buttons get the gestures
    QHash<QObject*, QAction*> buttons;    // button → its action, for the event filter
    QTimer* clickTimer = nullptr;         // the deferred single click (one at a time)
    QPointer<QWidget> anchor;             // set right before trigger → popover shape
    // The rect revealDialog animates from; dialogAnchorRect is the menu row's global rect for when
    // the icon is hidden.
    QPointer<QWidget> dialogAnchor;
    QRect dialogAnchorRect;
    // Recorded on QMenu::hovered — Qt hides the menu before emitting triggered().
    QPointer<QAction> menuRowAction;
    QRect menuRowRect;
    QPointer<QAction> pendingAction;      // the action a deferred click will trigger
    QPointer<QDialog> active;             // the popover being exec'd (outside-click close)
    QPointer<QWidget> overlay;            // the in-window box hosting it
    // A double-click already acted: swallow its trailing release, or it toggles a non-modal target
    // straight back off.
    bool swallowRelease = false;
    // A dismissing press must not re-open through the icon it landed on.
    bool dismissClick = false;
    // Releasing Alt closes exactly this; a dblclick / right-click open clears it and stays sticky.
    QPointer<QAction> peekAction;
    // The export-options popups are plain QMenus, kept apart from the popover machinery;
    // KeyRelease(Alt) closes unless the cursor moved inside.
    QPointer<QMenu> peekExportMenu;
    // Alt-glide: execMaybePopover rejects the current dialog and opens this one's peek next (the
    // modal loop blocks hover events).
    QPointer<QAction> peekNextAction;
    QPointer<QToolButton> peekNextButton;
    QTimer* lingerPoll = nullptr;

    // `consume` stops the press; `dismiss` closes the popover; `armDismissClick` keeps the icon
    // from re-opening what it just closed.
    struct Press {
      bool consume = false;
      bool dismiss = false;
      bool armDismissClick = false;
      bool clearPeek = false;
    };
    struct PressFacts {
      bool insidePopover = false;    // the point is within the popover's on-screen rect
      bool otherWindowOwns = false;  // a nested dialog / menu owns that point
      bool onLogo = false;           // …and the accent popover is the one that is open
      bool accentPopover = false;
      bool peekingAccent = false;    // the accent popover is an unengaged Alt peek
      bool onPopoverIcon = false;    // the press landed on the logo or a popover button
    };
    // Logo gestures never dismiss its accent popover (browser parity): right-press promotes to
    // sticky, left-press mid-peek is a no-op, left on sticky cycles under the list.
    static Press judgePress(const PressFacts& f, Qt::MouseButton button) {
      if (f.insidePopover || f.otherWindowOwns) return {};
      if (f.onLogo && f.accentPopover) {
        if (button == Qt::RightButton) return {true, false, false, true};
        if (button == Qt::LeftButton) return {f.peekingAccent, false, false, false};
      }
      return {false, true, f.onPopoverIcon, false};
    }
  };

}  // namespace stencil::gui
