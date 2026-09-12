#pragma once
#include <QColor>
#include <functional>
#include <QPointer>
#include <QRect>
#include <QString>

#include "motionPrefs.hpp"   // motionReduced() / dustAllowed() / dustMotionOk()

class QDialog;
class QWidget;

// The Qt half of the browser's `modalFromIcon`/`modalToIcon` (ui/base.js).
namespace stencil::support {

  // Flush pending layout, scrollbar decisions included. Exposed for the GUI test.
  void settleLayout(QWidget& w);

  // Call after the dialog is positioned, before exec(). exec() still returns when it
  // always did — the closing motion is a self-owned ghost window.
  void revealDialog(QDialog& dlg, QWidget* anchor);
  // `anchorRect` (GLOBAL) is the fallback origin when the icon is hidden.
  void revealDialog(QDialog& dlg, QWidget* anchor, const QRect& anchorRect);
  // `closeRect` (GLOBAL) aims the shrink elsewhere; invalid = fly back the way it came.
  // Browser twin: ui/base.js `backTo` / confirmModal.js `closeAnchor`.
  void revealDialog(QDialog& dlg, QWidget* anchor, const QRect& anchorRect,
                    const QRect& closeRect);

  // A box around the user's last press — the origin for a dialog nobody anchored.
  QRect gestureAnchorRect();

  // The same flight for a non-modal window: dismissWindow() hides it and flies a snapshot back.
  void revealWindow(QWidget& win, QWidget* anchor);
  void dismissWindow(QWidget& win, QWidget* anchor);

  // Non-native picker centred on `parent`; Cancel → invalid QColor(). `preview` is called
  // with every colour landed on, once more with `initial` on Cancel. `withAlpha` is
  // opt-in: only CSS-stored colours (#rrggbbaa) can carry one; a tint or accent drops the byte.
  QColor pickColorAnimated(const QColor& initial, QWidget* parent, const QString& title,
                           QWidget* anchor, const QRect& anchorRect = QRect(),
                           const std::function<void(const QColor&)>& preview = {},
                           bool withAlpha = false, const QRect& closeRect = QRect());

  // Application-wide watcher so QMessageBox::question and friends get the flight too.
  // Idempotent — every MainWindow calls it, and only the first one takes.
  void installDialogReveal();

  inline constexpr const char* kNoDialogRevealProperty = "stencilNoDialogReveal";

  // Click-outside dismissal (browser ui/base.js). Qt hands a modal's blocked windows
  // nothing, so this watches the press before QApplication drops it. Idempotent.
  void installModalDismiss();

  // On macOS a blocked window gets no QEvent at all, so modalDismissMac.mm reads the native press.
  void installModalDismissNative();

  // Written to the file named by STENCIL_MODAL_LOG; stderr is unreadable under LaunchServices.
  void modalDismissLog(const QString& line);

  inline constexpr const char* kNoOutsideDismissProperty = "stencilNoOutsideDismiss";

  // The SAME ceiling as DisintegrateOverlay::kSurfaceMaxCells and the browser's
  // SURFACE_COLS * SURFACE_ROWS = 46 * 30 (modalReveal.cpp static_asserts it).
  inline constexpr int kDialogDustMaxCells = 46 * 30;


}  // namespace stencil::support
