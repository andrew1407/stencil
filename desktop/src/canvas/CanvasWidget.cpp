#include "CanvasWidget.hpp"

#include <QApplication>

// The widget's construction. Everything it then does lives in the Canvas*.cpp partials
// beside it (image, settings, draw mode, selection, geometry, stroke fx, paint, input,
// render, hold-to-draw, transforms).

namespace stencil::gui {

  CanvasWidget::CanvasWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumSize(320, 240);
    setFocusPolicy(Qt::StrongFocus);
    applyDefaultsToCurrent();
    // Wheel edits (thickness/rotation) mutate live and commit one undo step once
    // the wheel goes quiet, mirroring the browser's debounced saveHistory.
    editCommitTimer_.setSingleShot(true);
    editCommitTimer_.setInterval(280);
    connect(&editCommitTimer_, &QTimer::timeout, this,
            [this] { commitHistory(); });
    // Hold-to-draw: while a hold is engaged, tick the controller (~40 ms) so the
    // hold/dwell thresholds fire even when the cursor is held perfectly still.
    holdClock_.start();
    holdTimer_.setInterval(40);
    connect(&holdTimer_, &QTimer::timeout, this, [this] { handleHoldTick(); });
    // Repaint at ~60fps while any just-added point is still travelling, and stop the moment the last
    // one lands (browser strokeFx.js drives the same loop off requestAnimationFrame).
    fxClock_.start();
    fxTimer_.setInterval(16);
    connect(&fxTimer_, &QTimer::timeout, this, [this] {
      // Only what is moving: a full-widget 60 Hz repaint redraws the whole zoomed page. Union the
      // frame before and after the step so the one that just landed is cleared too.
      const QRect before = strokeFxRect();
      if (!strokeFx_.step(fxNow())) fxTimer_.stop();
      update(before.united(strokeFxRect()));
    });
    // Watch modifier key changes app-wide so the tooltip/cursor refresh on
    // Shift/Ctrl/Alt without needing a mouse move (see eventFilter).
    qApp->installEventFilter(this);
  }

}  // namespace stencil::gui
