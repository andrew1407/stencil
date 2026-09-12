// eventFilter chain, the canvas and header-row handlers. Order and verdicts: MainWindowEvents.cpp.
#include "MainWindow.hpp"
#include "CanvasWidget.hpp"
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
    // Open the preset list on the click's RELEASE: showing it during the press lets the release land outside and dismiss it (macOS).
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
    // Alt-GLIDE onto the logo: its own copy of the shared peek route, since the logo is excluded from that block.
    if (obj == logoBtn_ && event->type() == QEvent::Enter &&
        QGuiApplication::queryKeyboardModifiers().testFlag(Qt::AltModifier)) {
      altPeekOpen(logoBtn_, actAccent_);
      return false;   // hover styling (LogoHoverFx) must still see the Enter
    }
    // Consumed whole: a stray Alt+click must not fall through to clicked() and fire the accent CYCLE.
    if (obj == logoBtn_ && event->type() == QEvent::MouseButtonPress) {
      auto* me = static_cast<QMouseEvent*>(event);
      if (me->button() == Qt::LeftButton && (me->modifiers() & Qt::AltModifier)) return true;
    }
    // Logo double-click → theme-colour picker (browser parity); cancels the pending single-click accent cycle first.
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
    // Zoom over the margin around a zoomed-out image; the position is already in viewport coordinates.
    if (scroll_ && obj == scroll_->viewport()) {
      const QEvent::Type t = event->type();
      if (t == QEvent::Resize) {
        positionOverlayArrows(); positionPanelReopenButton(); positionPanelGrip();
        positionChatEdge();
      }
      // Right-click on the margin opens the canvas context menu. With NO image the press is left alone (browser parity).
      if (t == QEvent::MouseButtonPress &&
          static_cast<QMouseEvent*>(event)->button() == Qt::RightButton &&
          canvas_ && canvas_->hasImage()) {
        showContextMenu(static_cast<QMouseEvent*>(event)->globalPosition().toPoint());
        return true;
      }
      // Plain LEFT press on the margin clears the selection (browser DrawingApp::deselectEmptyArea). Modifiers excluded:
      // Alt pans, Shift sweeps a zoom rect, Ctrl+Shift multi-selects.
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
    // Deferred so underMouse() settles — moving field→button stays "hovered".
    if (obj == nameBar_.group || obj == nameBar_.field || obj == nameBar_.edit
        || obj == nameBar_.colorBtn) {
      const QEvent::Type t = event->type();
      if (t == QEvent::Enter || t == QEvent::Leave)
        QTimer::singleShot(0, this, [this] { updateNameHover(); });
    }
    // A pointer landing ANYWHERE ELSE has left the group, whether or not the group's own Leave arrived.
    if (nameBar_.hover && obj != nameBar_.group && obj != nameBar_.field && obj != nameBar_.edit
        && obj != nameBar_.colorBtn
        && (event->type() == QEvent::Enter || event->type() == QEvent::HoverEnter
            || event->type() == QEvent::MouseMove || event->type() == QEvent::HoverMove))
      QTimer::singleShot(0, this, [this] { updateNameHover(); });
    if (obj == nameBar_.field) {
      const QEvent::Type t = event->type();
      if (t == QEvent::MouseButtonDblClick) {
        // Double-click a read-only name → edit mode (browser parity).
        if (!nameBar_.editing) {
          enterNameEdit();
          return true;
        }
      } else if (t == QEvent::KeyPress) {
        if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
          // Escape drops focus; mid-edit, revert too.
          if (nameBar_.editing) cancelProjectName();
          else nameBar_.field->clearFocus();
          return true;
        }
      } else if (t == QEvent::FocusOut) {
        // Deferred so a click on ✓ commits first.
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
