#pragma once
#include <QColor>
#include <functional>
#include <QPointer>
#include <QRect>
#include <QString>

#include "motionPrefs.hpp"   // motionReduced() / dustAllowed() / dustMotionOk()

class QDialog;
class QWidget;

// The Qt half of the browser's `modalFromIcon`/`modalToIcon` (css/animations.css +
// ui/base.js): a dialog grows out of the icon that opened it and shrinks back into it.
// A hidden or off-screen icon falls back to a point above the dialog.
namespace stencil::support {

  // Flush a window's pending layout, every scroll area's scrollbar decision included, so
  // a snapshot of it matches the window that lands. Exposed for the GUI test.
  void settleLayout(QWidget& w);

  // Animate `dlg` in from `anchor` and back into it on close. Call after the dialog is
  // positioned, before exec(); null/hidden `anchor` = the from-above fallback. exec()
  // still returns when it always did — the closing motion is a self-owned ghost window.
  void revealDialog(QDialog& dlg, QWidget* anchor);
  // …and this overload carries a GLOBAL rect to fall back on when the icon is hidden —
  // the menu row the command was picked from. Without it a dialog opened from the menu
  // bar while the toolbars are collapsed grew out of a generic box above itself, which
  // reads as dropping in from the top rather than opening from what you clicked.
  void revealDialog(QDialog& dlg, QWidget* anchor, const QRect& anchorRect);
  // …and this one aims the CLOSE somewhere else than the open: a dialog raised from a
  // context-menu row grows out of that row, which is gone by the time it closes, so
  // `closeRect` (GLOBAL) is the "⋯" the menu hung off. Invalid = fly back the way it came.
  // Browser twin: ui/base.js `backTo` / confirmModal.js `closeAnchor`.
  void revealDialog(QDialog& dlg, QWidget* anchor, const QRect& anchorRect,
                    const QRect& closeRect);

  // A box around the point the user last pressed, the origin for a dialog nobody anchored
  // (installDialogReveal uses it). Exposed so a call site that claims its own reveal can
  // keep the same open origin while aiming the close elsewhere.
  QRect gestureAnchorRect();

  // The same flight for a non-modal top-level window (the floating chat dock): call
  // revealWindow() right after showing it, and dismissWindow() instead of hiding it —
  // that one hides the window itself and flies a snapshot back into the icon.
  void revealWindow(QWidget& win, QWidget* anchor);
  void dismissWindow(QWidget& win, QWidget* anchor);

  // Animated drop-in for QColorDialog::getColor(): a non-native picker centred on
  // `parent` that flies out of / back into `anchor` via revealDialog. Cancel → invalid
  // QColor(), same contract as getColor. `anchorRect` (GLOBAL) is the fallback origin.
  //
  // `preview` is called with every colour the user lands on, so the choice is applied to
  // the real thing as it is made; Cancel calls it once more with `initial`.
  //
  // `withAlpha` shows the alpha slider — opt-in, because only CSS-stored colours (a line,
  // its points, an area fill) can carry one; a tint or accent is parsed as plain #rrggbb
  // everywhere and would drop the byte. `closeRect` (GLOBAL) aims the shrink, as above.
  QColor pickColorAnimated(const QColor& initial, QWidget* parent, const QString& title,
                           QWidget* anchor, const QRect& anchorRect = QRect(),
                           const std::function<void(const QColor&)>& preview = {},
                           bool withAlpha = false, const QRect& closeRect = QRect());

  // Install the application-wide watcher that gives EVERY dialog the flight — including
  // the ones nobody wires by hand: QMessageBox::question and friends, which are built and
  // exec'd in one expression from a dozen call sites and used to appear with no motion at
  // all. A dialog revealDialog() already owns is skipped; one that has neither an anchor
  // widget nor a caller who knows where the command came from flies out of the point the
  // user last pressed, which is the honest origin for a question you just provoked.
  // Idempotent — every MainWindow calls it, and only the first one takes.
  void installDialogReveal();

  // Set on a dialog that must never take that automatic flight.
  inline constexpr const char* kNoDialogRevealProperty = "stencilNoDialogReveal";

  // Click-outside dismissal, the browser's rule (ui/base.js: a press on the modal overlay
  // closes the box). Qt hands a modal's blocked windows nothing, so a click on the main
  // window did nothing at all here (user report) — this watches the press before
  // QApplication drops it and rejects the modal instead. Idempotent.
  void installModalDismiss();

  // …and its platform half, where one exists. A window a modal blocks never gets a mouse
  // event from Qt (QtGui drops it before any QEvent is made), so on macOS the press is
  // read from the native event instead — modalDismissMac.mm. A no-op elsewhere.
  void installModalDismissNative();

  // Set on a dialog that must be answered, never clicked away.
  inline constexpr const char* kNoOutsideDismissProperty = "stencilNoOutsideDismiss";

  // A window-sized dust cloud's mote budget — the SAME ceiling every other surface flight
  // uses (DisintegrateOverlay::kSurfaceMaxCells, the browser's SURFACE_COLS * SURFACE_ROWS
  // = 46 * 30; modalReveal.cpp static_asserts the two agree). It used to override that
  // default with 4000, which packed nearly three times the grains into the icon the window
  // pours out of: at the far end they overlapped into a solid chunk of the accent instead
  // of dust (user report). Also used by execMaybePopover's popover flight.
  inline constexpr int kDialogDustMaxCells = 46 * 30;

  // The motion preferences every helper here checks — motionReduced(), dustAllowed(),
  // dustMotionOk(), drawingMotionOk() — live in their own header-only home, included
  // above so the dozen call sites that reach for them through this one keep working.

}  // namespace stencil::support
