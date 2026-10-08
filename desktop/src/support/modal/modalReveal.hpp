#pragma once
#include <QColor>
#include <functional>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QWidget>

#include "motionPrefs.hpp"   // motionReduced() / isDustAllowed() / isDustMotionOk()

class QDialog;
class QWidget;

// The Qt half of the browser's `modalFromIcon`/`modalToIcon` (ui/base.js).
namespace stencil::support {

  // The window a dialog sits over (its parent's top level); null for an unparented one.
  inline QWidget* dialogHost(const QWidget* dlg) {
    return dlg && dlg->parentWidget() ? dlg->parentWidget()->window() : nullptr;
  }

  // Flush pending layout, scrollbar decisions included. Exposed for the GUI test.
  void settleLayout(QWidget& w);

  // Call after the dialog is positioned, before exec(). exec() still returns when it
  // always did — the closing motion is a self-owned ghost window.
  void revealDialog(QDialog& dlg, QWidget* anchor);
  // `anchorRect` (GLOBAL) is the fallback origin when the icon is hidden.
  void revealDialog(QDialog& dlg, QWidget* anchor, const QRect& anchorRect);
  // `closeRect` (GLOBAL) aims the shrink elsewhere; invalid = fly back the way it came, and
  // `closeRectFor(accepted)` asks for it at hide time (browser confirmModal.js `closeAnchor`).
  void revealDialog(QDialog& dlg, QWidget* anchor, const QRect& anchorRect,
                    const QRect& closeRect);
  void revealDialog(QDialog& dlg, QWidget* anchor, const QRect& anchorRect, const QRect& closeRect,
                    std::function<QRect(bool)> closeRectFor);

  // The origin for a dialog nobody anchored: a box around the last press; after a shortcut, the
  // icon it stands for, or invalid (the flight falls from above) when that is folded away.
  QRect gestureAnchorRect();
  // The icon an action about to run stands for (null when it has no visible one).
  void noteActionAnchor(QWidget* icon);
  // Where a close rises to when the home it names is folded away (browser shell.js: up, not the canvas).
  QRect riseRect(const QDialog& dlg);

  // A box's top-left on `at`, moved left or up as far as keeps it inside `avail` (as given when invalid).
  inline QPoint topLeftAt(QPoint at, const QSize& box, const QRect& avail) {
    if (!avail.isValid()) return at;
    at.setX(qMax(qMin(at.x(), avail.right() + 1 - box.width()), avail.left()));
    at.setY(qMax(qMin(at.y(), avail.bottom() + 1 - box.height()), avail.top()));
    return at;
  }

  // A box's top-left with its centre on `centre`, kept inside `avail` (unclamped when invalid).
  inline QPoint centredTopLeft(const QPoint& centre, const QSize& box, const QRect& avail) {
    return topLeftAt(QPoint(centre.x() - box.width() / 2, centre.y() - box.height() / 2), box, avail);
  }

  // A window a drop opens grows out of a box this wide on the cursor (browser DROP_ANCHOR_PX, ui/drag/iconDrag.js).
  inline constexpr int CURSOR_ORIGIN_PX = 24;
  inline QRect cursorOrigin(const QPoint& at) {
    return QRect(at - QPoint(CURSOR_ORIGIN_PX / 2, CURSOR_ORIGIN_PX / 2), QSize(CURSOR_ORIGIN_PX, CURSOR_ORIGIN_PX));
  }

  // While in scope, the next dialog to show opens with its frame's top-left on `at` (GLOBAL), kept on
  // its host's screen, and grows out of the cursor there (its close still flies to its opener); a
  // later dialog is untouched.
  class DialogLanding {
   public:
    explicit DialogLanding(const QPoint& at);
    ~DialogLanding();
    DialogLanding(const DialogLanding&) = delete;
    DialogLanding& operator=(const DialogLanding&) = delete;
  };

  // The same flight for a non-modal window: dismissWindow() hides it and flies a snapshot back.
  // veilForReveal() goes BEFORE the window maps: shown first, it stood whole for a frame.
  void veilForReveal(QWidget& win);
  // `from` (GLOBAL) is where the reveal grows out of in place of `anchor`, the window it flies over.
  void revealWindow(QWidget& win, QWidget* anchor, const QRect& from = QRect());
  void dismissWindow(QWidget& win, QWidget* anchor);

  // Non-native picker centred on `parent`; Cancel -> invalid QColor(). `preview` is called with every
  // colour landed on. `withAlpha` is opt-in: only CSS-stored colours (#rrggbbaa) can carry one.
  QColor pickColorAnimated(const QColor& initial, QWidget* parent, const QString& title,
                           QWidget* anchor, const QRect& anchorRect = QRect(),
                           const std::function<void(const QColor&)>& preview = {},
                           bool withAlpha = false, const QRect& closeRect = QRect());

  // Application-wide watcher so QMessageBox::question and friends get the flight too.
  // Idempotent — every MainWindow calls it, and only the first one takes.
  void installDialogReveal();

  inline constexpr const char* NO_DIALOG_REVEAL_PROPERTY = "stencilNoDialogReveal";

  // Every parented dialog window lands with its frame on its host's client centre. Idempotent.
  void installDialogCentring();

  // Click-outside dismissal (browser ui/base.js). Qt hands a modal's blocked windows
  // nothing, so this watches the press before QApplication drops it. Idempotent.
  void installModalDismiss();

  // On macOS a blocked window gets no QEvent at all, so modalDismissMac.mm reads the native press.
  void installModalDismissNative();

  // Written to the file named by STENCIL_MODAL_LOG; stderr is unreadable under LaunchServices.
  // The variable is read once; a caller asks modalDismissLogOn() before it formats a line.
  bool modalDismissLogOn();
  void modalDismissLog(const QString& line);

  inline constexpr const char* NO_OUTSIDE_DISMISS_PROPERTY = "stencilNoOutsideDismiss";

  // The SAME ceiling as DisintegrateOverlay::SURFACE_MAX_CELLS and the browser's
  // SURFACE_COLS * SURFACE_ROWS = 46 * 30 (modalReveal.cpp static_asserts it).
  inline constexpr int DIALOG_DUST_MAX_CELLS = 46 * 30;


}  // namespace stencil::support
