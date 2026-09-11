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

  // Modal popovers (support/popover.hpp). Dialog-opening toolbar icons answer a second
  // gesture set: double-click / right-click opens the SAME dialog as a compact frameless
  // popover pinned next to the icon. A plain click still opens the full dialog — deferred
  // one double-click interval (logo pattern) so the blocking exec() can never swallow the
  // second click of a double-click. This holds that machinery's state and the one rule
  // that decides what a press outside an open popover means; MainWindow drives the widgets.
  class PopoverHost {
   public:
    QSet<QAction*> dialogActions;         // the actions whose buttons get the gestures
    QHash<QObject*, QAction*> buttons;    // button → its action, for the event filter
    QTimer* clickTimer = nullptr;         // the deferred single click (one at a time)
    QPointer<QWidget> anchor;             // set right before trigger → popover shape
    // Icon that last opened a dialog (click, menu or shortcut) — the rect
    // support::revealDialog animates the window out of and back into. dialogAnchorRect is
    // the MENU row's global rect, used when that icon is hidden (toolbars collapsed) so
    // the window still opens from what was clicked.
    QPointer<QWidget> dialogAnchor;
    QRect dialogAnchorRect;
    // The menu row under the cursor/selection right now, recorded on QMenu::hovered — Qt
    // hides the menu BEFORE emitting triggered(), so the row has to be captured while the
    // popup is still up. Cleared one cycle after the menu hides (buildMenus).
    QPointer<QAction> menuRowAction;
    QRect menuRowRect;
    QPointer<QAction> pendingAction;      // the action a deferred click will trigger
    QPointer<QDialog> active;             // the popover being exec'd (outside-click close)
    QPointer<QWidget> overlay;            // the in-window box hosting it
    // A double-click already acted for this press cycle: swallow its trailing RELEASE
    // without re-arming the deferred click, which would toggle a NON-modal target (the
    // chat dock) straight back off. Reset on the next press.
    bool swallowRelease = false;
    // A press dismissed a popover: the same click must not go on to RE-OPEN it through
    // the icon it landed on (the logo's accent cycle, a popover icon's deferred click).
    bool dismissClick = false;
    // HOLD-to-peek: the action whose popover an Alt+hover opened. Releasing Alt closes
    // exactly that (reject the modal popover / hide the compact chat) and nothing else —
    // a dblclick / right-click open clears this and stays sticky.
    QPointer<QAction> peekAction;
    // HOLD-to-peek for the export-options popups: the same gesture as `buttons`, but a
    // plain QMenu (QMenu::popup(), not a QDialog trigger), so it is kept independent of
    // the popover machinery. Set right after popup(); KeyRelease(Alt) closes it unless
    // the cursor has since moved INSIDE it (engaged, as for a peeked popover).
    QPointer<QMenu> peekExportMenu;
    // Alt-GLIDE continuation: the popover icon the cursor landed on while another popover
    // was showing; execMaybePopover rejects the current dialog and opens this one's peek
    // next (the modal loop blocks ordinary hover events).
    QPointer<QAction> peekNextAction;
    QPointer<QToolButton> peekNextButton;
    // Poll that hover-binds a LINGERING window (engaged peek after Alt release): closes it
    // once the cursor leaves, unless it holds typed content.
    QTimer* lingerPoll = nullptr;

    // What a press means while a popover is open, decided from facts the caller has
    // already established. `consume` stops the press travelling on; `dismiss` closes the
    // popover; `armDismissClick` marks the click so the icon it lands on does not
    // immediately re-open what it just closed.
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
    // Gestures on the LOGO while its accent popover is up never dismiss it (browser
    // parity): a RIGHT-press promotes a peek to sticky, a LEFT-press mid-peek is a no-op
    // (both consumed), and a LEFT-press on a sticky popover travels on to the logo, whose
    // click cycles the accent under the open list. Every other press dismisses.
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
