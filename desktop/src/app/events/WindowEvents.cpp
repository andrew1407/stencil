// eventFilter chain, the canvas and header-row handlers. Order and verdicts: MainWindowEvents.cpp.
#include "MainWindow.hpp"
#include "ProjectTitleController.hpp"
#include "WindowEvents.hpp"
#include "CanvasWidget.hpp"
#include "modalReveal.hpp"   // pickColorAnimated
#include "comboAltPeek.hpp"  // altKeyHeld
#include "theme.hpp"
#include <QComboBox>
#include <QLineEdit>
#include <QScrollArea>
#include <QToolButton>

namespace stencil::gui {

  std::optional<bool> WindowEvents::filterZoomAndLogo(QObject* obj, QEvent* event) {
    // Open the preset list on the click's RELEASE: showing it during the press lets the release land outside and dismiss it (macOS).
    if (w.zoom && obj == w.zoom->lineEdit()) {
      const auto openPopup = [this] {
        QTimer::singleShot(0, &w, [this] {
          if (w.zoom && w.zoom->lineEdit()->hasFocus()) w.zoom->showPopup();
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
    if (obj == w.tools.logoBtn && event->type() == QEvent::Enter && support::altKeyHeld()) {
      w.parts.popoverGestures.altPeekOpenSoon(w.tools.logoBtn, w.acts.accent);
      return false;   // hover styling (LogoHoverFx) must still see the Enter
    }
    // Consumed whole: a stray Alt+click must not fall through to clicked() and fire the accent CYCLE.
    if (obj == w.tools.logoBtn && event->type() == QEvent::MouseButtonPress) {
      auto* me = static_cast<QMouseEvent*>(event);
      if (me->button() == Qt::LeftButton && (me->modifiers() & Qt::AltModifier)) return true;
    }
    // Logo double-click → theme-colour picker (browser parity); cancels the pending single-click accent cycle first.
    if (obj == w.tools.logoBtn && event->type() == QEvent::MouseButtonDblClick) {
      if (w.tools.logoClickTimer) w.tools.logoClickTimer->stop();
      const QColor cur = accentPrimary(w.settings.accentColor);
      const QColor c = support::pickColorAnimated(cur, &w, "Theme color", w.tools.logoBtn);
      if (c.isValid()) {
        auto next = w.settings;
        next.accentColor = c.name();   // store as hex → custom accent (accentPrimary handles it)
        w.applySettings(next, true);
      }
      return true;
    }
    return {};   // nothing here answered — the chain goes on
  }

  std::optional<bool> WindowEvents::filterCanvasViewport(QObject* obj, QEvent* event) {
    // Zoom over the margin around a zoomed-out image; the position is already in viewport coordinates.
    if (w.scroll && obj == w.scroll->viewport()) {
      const QEvent::Type t = event->type();
      if (t == QEvent::Resize) {
        w.positionOverlayArrows(); w.parts.dockChrome.positionPanelReopenButton(); w.parts.dockChrome.positionPanelGrip();
        w.parts.dockChrome.positionChatEdge();
      }
      // Right-click on the margin opens the canvas context menu. With NO image the press is left alone (browser parity).
      if (t == QEvent::MouseButtonPress &&
          static_cast<QMouseEvent*>(event)->button() == Qt::RightButton &&
          w.canvas && w.canvas->hasImage()) {
        w.parts.canvasMenu.showContextMenu(static_cast<QMouseEvent*>(event)->globalPosition().toPoint());
        return true;
      }
      // Plain LEFT press on the margin clears the selection (browser DrawingApp::deselectEmptyArea). Modifiers excluded:
      // Alt pans, Shift sweeps a zoom rect, Ctrl+Shift multi-selects.
      if (t == QEvent::MouseButtonPress &&
          static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton &&
          static_cast<QMouseEvent*>(event)->modifiers() == Qt::NoModifier &&
          w.canvas && w.canvas->hasImage() && !w.canvas->compareReadOnly() &&
          !w.canvas->selectedIndices().empty()) {
        w.canvas->deselect();
        return true;
      }
      if (t == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(event);
        if (we->modifiers() & Qt::ControlModifier) {
          const QPoint d = we->angleDelta();
          const int delta = d.y() != 0 ? d.y() : d.x();
          if (delta != 0) {
            const double step = (we->modifiers() & Qt::ShiftModifier) ? 0.3 : 0.1;
            w.setZoomAnchored(w.canvas->getScale() + (delta > 0 ? step : -step),
                              we->position().toPoint());
            return true;
          }
        }
      } else if (t == QEvent::NativeGesture) {
        auto* g = static_cast<QNativeGestureEvent*>(event);
        if (g->gestureType() == Qt::ZoomNativeGesture) {
          const double factor = 1.0 + g->value();
          if (factor > 0.0 && factor != 1.0)
            w.setZoomAnchored(w.canvas->getScale() * factor, g->position().toPoint());
          return true;
        }
      }
    }
    return {};   // nothing here answered — the chain goes on
  }

  std::optional<bool> WindowEvents::filterProjectNameBar(QObject* obj, QEvent* event) {
    // Deferred so underMouse() settles — moving field→button stays "hovered".
    if (obj == w.nameBar.group || obj == w.nameBar.field || obj == w.nameBar.edit
        || obj == w.nameBar.colorBtn) {
      const QEvent::Type t = event->type();
      if (t == QEvent::Enter || t == QEvent::Leave)
        QTimer::singleShot(0, &w, [this] { w.projectTitle->updateNameHover(); });
    }
    // A pointer landing ANYWHERE ELSE has left the group, whether or not the group's own Leave arrived.
    if (w.nameBar.hover && obj != w.nameBar.group && obj != w.nameBar.field && obj != w.nameBar.edit
        && obj != w.nameBar.colorBtn
        && (event->type() == QEvent::Enter || event->type() == QEvent::HoverEnter
            || event->type() == QEvent::MouseMove || event->type() == QEvent::HoverMove))
      QTimer::singleShot(0, &w, [this] { w.projectTitle->updateNameHover(); });
    if (obj == w.nameBar.field) {
      const QEvent::Type t = event->type();
      if (t == QEvent::MouseButtonDblClick) {
        // Double-click a read-only name → edit mode (browser parity).
        if (!w.nameBar.editing) {
          w.projectTitle->enterNameEdit();
          return true;
        }
      } else if (t == QEvent::KeyPress) {
        if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
          // Escape drops focus; mid-edit, revert too.
          if (w.nameBar.editing) w.projectTitle->cancelProjectName();
          else w.nameBar.field->clearFocus();
          return true;
        }
      } else if (t == QEvent::FocusOut) {
        // Deferred so a click on ✓ commits first.
        if (w.nameBar.editing) {
          QTimer::singleShot(0, &w, [this] {
            if (w.nameBar.editing && w.nameBar.field && !w.nameBar.field->hasFocus()) w.projectTitle->cancelProjectName();
          });
        }
      }
    }
    return {};   // nothing here answered — the chain goes on
  }

}  // namespace stencil::gui
