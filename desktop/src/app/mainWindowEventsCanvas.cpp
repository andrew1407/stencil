// eventFilter chain, the canvas and header-row handlers: the zoom field's preset popup, the
// logo's Alt/dblclick gestures, the viewport margin around a zoomed-out image, and the project
// name group's hover-reveal and inline edit. Order and verdicts: mainWindowEvents.cpp.
#include "mainWindow.hpp"
#include "canvasWidget.hpp"
#include "theme.hpp"
#include "modalReveal.hpp"   // pickColorAnimated
#include <QAbstractItemView>
#include <QComboBox>
#include <QCursor>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QScrollArea>
#include <QToolButton>
#include <QTimer>
#include <QWheelEvent>
#include <optional>

namespace stencil::gui {

  std::optional<bool> MainWindow::filterZoomAndLogo(QObject* obj, QEvent* event) {
    // Zoom field → open the preset list without the separate arrow. Trigger on the click's
    // mouse-RELEASE (not press/focus): showing the popup during the press cycle lets the pending
    // release land outside it and immediately dismiss it (macOS), so it just flashed. Tab/keyboard
    // focus opens it too. The field stays editable, so the user can still type over the popup.
    if (zoom_ && obj == zoom_->lineEdit()) {
      const auto openPopup = [this] {
        QTimer::singleShot(0, this, [this] {
          if (zoom_ && zoom_->lineEdit()->hasFocus() && !zoom_->view()->isVisible()) zoom_->showPopup();
        });
      };
      if (event->type() == QEvent::MouseButtonRelease &&
          static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        openPopup();
      } else if (event->type() == QEvent::FocusIn) {
        const auto reason = static_cast<QFocusEvent*>(event)->reason();
        if (reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason ||
            reason == Qt::ShortcutFocusReason)
          openPopup();
      }
      return false;   // never consume — the field's caret / typing must behave normally
    }
    // Alt-GLIDE onto the logo (Alt already held, cursor arrives): the same peek route
    // the shared block below gives every popover icon — the logo has its own copy
    // because it is excluded from that block to protect its click/dblclick gestures.
    if (obj == logoBtn_ && event->type() == QEvent::Enter &&
        QGuiApplication::queryKeyboardModifiers().testFlag(Qt::AltModifier)) {
      altPeekOpen(logoBtn_, actAccent_);
      return false;   // hover styling (LogoHoverFx) must still see the Enter
    }
    // Alt+left-press on the logo is consumed whole: the Alt keypress (or glide)
    // already owns the peek, and a stray Alt+click must not fall through to clicked()
    // and fire the accent CYCLE mid-Alt-gesture.
    if (obj == logoBtn_ && event->type() == QEvent::MouseButtonPress) {
      auto* me = static_cast<QMouseEvent*>(event);
      if (me->button() == Qt::LeftButton && (me->modifiers() & Qt::AltModifier)) return true;
    }
    // Logo double-click → custom theme-colour picker (browser parity). Cancels the pending single-
    // click accent-cycle first, then opens the non-native colour dialog seeded with the current accent.
    if (obj == logoBtn_ && event->type() == QEvent::MouseButtonDblClick) {
      if (logoClickTimer_) logoClickTimer_->stop();
      const QColor cur = accentPrimary(settings_.accentColor);
      const QColor c = support::pickColorAnimated(cur, this, "Theme color", logoBtn_);
      if (c.isValid()) {
        auto next = settings_;
        next.accentColor = c.name();   // store as hex → custom accent (accentPrimary handles it)
        applySettings(next, true);
      }
      return true;
    }
    return {};   // nothing here answered — the chain goes on
  }

  std::optional<bool> MainWindow::filterCanvasViewport(QObject* obj, QEvent* event) {
    // Zoom over the empty margin around a zoomed-out image (the viewport, not the
    // canvas). Mirrors CanvasWidget's Ctrl+wheel / pinch zoom; the event position is
    // already in viewport coordinates, which is what setZoomAnchored wants.
    if (scroll_ && obj == scroll_->viewport()) {
      const QEvent::Type t = event->type();
      if (t == QEvent::Resize) {
        positionOverlayArrows(); positionPanelReopenButton(); positionPanelGrip();
        positionChatEdge();
      }
      // Right-click on the margin around the image opens the same context menu the
      // canvas opens — the backdrop had none. With NO image the press is left alone:
      // showContextMenu() opens nothing then (browser contextMenu.js parity), so
      // swallowing the click here would only make the backdrop eat it.
      if (t == QEvent::MouseButtonPress &&
          static_cast<QMouseEvent*>(event)->button() == Qt::RightButton &&
          canvas_ && canvas_->hasImage()) {
        showContextMenu(static_cast<QMouseEvent*>(event)->globalPosition().toPoint());
        return true;
      }
      // Plain LEFT press on that same margin clears the selection — clicking empty
      // space inside the image already does (CanvasWidget::selectLineAt hit-tests to
      // nothing), but the backdrop around a zoomed-out image never reached the canvas,
      // so a selection got stuck there. Modifiers are excluded: Alt pans, Shift sweeps
      // a zoom rect and Ctrl+Shift multi-selects, and those gestures start on the
      // margin too. Browser counterpart: DrawingApp::deselectEmptyArea.
      if (t == QEvent::MouseButtonPress &&
          static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton &&
          static_cast<QMouseEvent*>(event)->modifiers() == Qt::NoModifier &&
          canvas_ && canvas_->hasImage() && !canvas_->compareReadOnly() &&
          !canvas_->selectedIndices().empty()) {
        canvas_->deselect();
        return true;
      }
      if (t == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(event);
        if (we->modifiers() & Qt::ControlModifier) {
          const QPoint d = we->angleDelta();
          const int delta = d.y() != 0 ? d.y() : d.x();
          if (delta != 0) {
            const double step = (we->modifiers() & Qt::ShiftModifier) ? 0.3 : 0.1;
            setZoomAnchored(canvas_->scale() + (delta > 0 ? step : -step),
                            we->position().toPoint());
            return true;
          }
        }
        // Plain wheel over the margin → let the scroll area scroll.
      } else if (t == QEvent::NativeGesture) {
        auto* g = static_cast<QNativeGestureEvent*>(event);
        if (g->gestureType() == Qt::ZoomNativeGesture) {
          const double factor = 1.0 + g->value();
          if (factor > 0.0 && factor != 1.0)
            setZoomAnchored(canvas_->scale() * factor, g->position().toPoint());
          return true;
        }
      }
    }
    return {};   // nothing here answered — the chain goes on
  }

  std::optional<bool> MainWindow::filterProjectNameBar(QObject* obj, QEvent* event) {
    // Hover-reveal for the name group: any Enter/Leave on the field or the ✎/🎨 buttons recomputes
    // hover (deferred so underMouse() settles — moving field→button stays "hovered", no flicker).
    if (obj == nameBar_.group || obj == nameBar_.field || obj == nameBar_.edit
        || obj == nameBar_.colorBtn) {
      const QEvent::Type t = event->type();
      if (t == QEvent::Enter || t == QEvent::Leave)
        QTimer::singleShot(0, this, [this] { updateNameHover(); });
    }
    // …and a pointer that has landed ANYWHERE ELSE has left the group, whether or not the
    // group's own Leave arrived: crossing straight onto another row's icon left the ✎/🎨
    // lit while the pointer was three clusters away. Only
    // while the hover is actually held, so this costs nothing the rest of the time.
    if (nameBar_.hover && obj != nameBar_.group && obj != nameBar_.field && obj != nameBar_.edit
        && obj != nameBar_.colorBtn
        && (event->type() == QEvent::Enter || event->type() == QEvent::HoverEnter
            || event->type() == QEvent::MouseMove || event->type() == QEvent::HoverMove))
      QTimer::singleShot(0, this, [this] { updateNameHover(); });
    if (obj == nameBar_.field) {
      const QEvent::Type t = event->type();
      if (t == QEvent::MouseButtonDblClick) {
        // Double-click a read-only name → enter edit mode (browser parity).
        if (!nameBar_.editing) {
          enterNameEdit();
          return true;
        }
      } else if (t == QEvent::KeyPress) {
        if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
          // Escape always drops focus (clears the outline). If mid-edit, revert too.
          if (nameBar_.editing) cancelProjectName();
          else nameBar_.field->clearFocus();
          return true;
        }
      } else if (t == QEvent::FocusOut) {
        // Clicking away leaves the edit: revert. Deferred so a click on ✓ commits first
        // (after which the field no longer has focus AND nameBar_.editing is already false → no-op).
        if (nameBar_.editing) {
          QTimer::singleShot(0, this, [this] {
            if (nameBar_.editing && nameBar_.field && !nameBar_.field->hasFocus()) cancelProjectName();
          });
        }
      }
    }
    return {};   // nothing here answered — the chain goes on
  }

}  // namespace stencil::gui
