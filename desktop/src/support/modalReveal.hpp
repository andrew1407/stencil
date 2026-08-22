#pragma once
#include <QColor>
#include <functional>
#include <QPointer>
#include <QRect>
#include <QString>

class QDialog;
class QWidget;

// The Qt half of the browser's `modalFromIcon`/`modalToIcon` (css/animations.css +
// ui/base.js): a dialog grows out of the icon that opened it and shrinks back into it.
// A hidden or off-screen icon falls back to a point above the dialog.
namespace stencil::support {

  // Animate `dlg` in from `anchor` and back into it on close. Call after the dialog is
  // positioned, before exec(); null/hidden `anchor` = the from-above fallback. exec()
  // still returns when it always did — the closing motion is a self-owned ghost window.
  void revealDialog(QDialog& dlg, QWidget* anchor);
  // …and this overload carries a GLOBAL rect to fall back on when the icon is hidden —
  // the menu row the command was picked from. Without it a dialog opened from the menu
  // bar while the toolbars are collapsed grew out of a generic box above itself, which
  // reads as dropping in from the top rather than opening from what you clicked.
  void revealDialog(QDialog& dlg, QWidget* anchor, const QRect& anchorRect);

  // The same flight for a non-modal top-level window (the floating chat dock): call
  // revealWindow() right after showing it, and dismissWindow() instead of hiding it —
  // that one hides the window itself and flies a snapshot back into the icon.
  void revealWindow(QWidget& win, QWidget* anchor);
  void dismissWindow(QWidget& win, QWidget* anchor);

  // Animated drop-in for QColorDialog::getColor(): a non-native picker that lands
  // centred on `parent` like getColor's, but flies out of / back into `anchor` via
  // revealDialog. Cancel → invalid QColor(), same contract as getColor. `anchorRect`
  // (GLOBAL) is the fallback origin when there is no anchor widget (e.g. a list row).
  QColor pickColorAnimated(const QColor& initial, QWidget* parent, const QString& title,
                           QWidget* anchor, const QRect& anchorRect = QRect());

  // Animations suppressed (STENCIL_NO_ANIM=1).
  bool motionReduced();

}  // namespace stencil::support
