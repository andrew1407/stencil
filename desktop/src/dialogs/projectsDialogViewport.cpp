// The Projects dialog's list-viewport event handler: the row "⋯" kebab strip, the hover-magnify
// preview's own hit-testing, and the press bookkeeping the open gestures read. One link in the
// eventFilter chain — order and verdicts in projectsDialog.cpp.
#include "projectsDialog.hpp"
#include "projectRowDelegate.hpp"
#include "projectsRowChrome.hpp"
#include "serverClient.hpp"
#include "../support/shimmerOverlay.hpp"
#include <QApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QTimer>
#include <optional>
#include "reorderableListWidget.hpp"
#include "projectDragZones.hpp"
#include "../support/scrollReveal.hpp"
#include "../support/appTooltip.hpp"
namespace stencil::gui {

  std::optional<bool> ProjectsDialog::filterListViewport(QObject* obj, QEvent* ev) {
    if (list_ && obj == list_->viewport()) {
      // On a width change, recompute item sizeHints so rows re-clamp + re-elide to the new
      // viewport width (the delegate caps width to the viewport).
      if (ev->type() == QEvent::Resize) list_->doItemsLayout();
      // Left-click on the "⋯" kebab strip pops the row's menu (consume it so it
      // doesn't also start a drag/selection); it's the right-click menu's twin.
      // The magnified preview IS the tooltip — never stack the text one on it.
      if (ev->type() == QEvent::ToolTip && hoverPreview_ && hoverPreview_->isVisible())
        return true;
      if (ev->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(ev);
        // itemClicked/itemDoubleClicked carry no modifiers — remember the ones
        // that were down for the press that produces them (and where it landed,
        // for the name-dblclick rename hit test).
        pressMods_ = me->modifiers();
        const QPoint vpos = me->position().toPoint();
        pressPos_ = vpos;
        QListWidgetItem* it = list_->itemAt(vpos);
        // A press on the checkbox strip is a SELECTION gesture: it must toggle
        // the box and nothing else (no row-open, no confirm, dialog stays).
        const QRect vr = it ? list_->visualItemRect(it) : QRect();
        pressOnCheck_ = me->button() == Qt::LeftButton && it &&
                        QRect(vr.left(), vr.top(), 34, vr.height()).contains(vpos);
        if (me->button() == Qt::LeftButton && it &&
            !it->data(Qt::UserRole).isNull() &&
            kebabZone(list_->visualItemRect(it)).contains(vpos)) {
          list_->setCurrentItem(it);
          showRowMenu(it, me->globalPosition().toPoint());
          return true;
        }
      }
      if (ev->type() == QEvent::ToolTip) {
        // The rows' tooltips go through the app's tooltip, not Qt's plain label:
        // AppTooltip's filter skips item views by design (they resolve a per-index tip in
        // viewportEvent), so these rows were the one place still showing Qt's box.
        auto* he = static_cast<QHelpEvent*>(ev);
        QListWidgetItem* it = list_->itemAt(he->pos());
        // The "⋯" carries its OWN tip, as the browser's per-row button does
        // (projectsModal.js menuBtn.title), not the row's picture info.
        const bool onKebab = it && !it->data(Qt::UserRole).isNull() &&
                             kebabZone(list_->visualItemRect(it)).contains(he->pos());
        const QString tip = !it ? QString()
                                : (onKebab ? kKebabTip : it->toolTip());
        if (tip.isEmpty()) { gui::appTooltip()->hideTip(); return true; }
        // …forming out of the CURSOR, which is where the browser's own tooltip flies from
        // and back into (ui/tooltip.js dust). Left to the default it grew out of the
        // owner's centre — here the whole viewport, i.e. the middle of the list.
        tipRowText_ = tip;
        gui::appTooltip()->showFor(list_->viewport(), tip, he->globalPos(),
                                   QRect(he->globalPos(), QSize(1, 1)));
        return true;
      }
      if (ev->type() == QEvent::MouseMove) {
        const QPoint vpos = static_cast<QMouseEvent*>(ev)->position().toPoint();
        QListWidgetItem* it = list_->itemAt(vpos);
        // Which row's "⋯" the pointer is actually on — the chip styles itself only for
        // that, never for a hover anywhere else on the row.
        const bool onKebab = it && !it->data(Qt::UserRole).isNull() &&
                             kebabZone(list_->visualItemRect(it)).contains(vpos);
        setKebabHover(onKebab ? list_->row(it) : -1);
        // …and the tooltip TRAVELS with the pointer while it is up: moveTo SLIDES it, with
        // no re-measure and no entrance. Going back through showFor on every move re-ran
        // its appearance bookkeeping and made the tip stutter and jump.
        if (auto* tip = gui::appTooltip(); tip->isVisible() && tip->owner() == list_->viewport()) {
          // Which tip belongs HERE — the kebab's own, or the row's picture info. Reading
          // only the row's would slide it on over the "⋯", where a different tip is due.
          const QString text = !it ? QString() : (onKebab ? kKebabTip : it->toolTip());
          if (text.isEmpty() || text != tipRowText_) tip->hideTip();
          else tip->moveTo(static_cast<QMouseEvent*>(ev)->globalPosition().toPoint());
        }
        const QPixmap src = it ? it->data(Qt::UserRole + 2).value<QPixmap>() : QPixmap();
        // Magnify only while over the THUMBNAIL itself — the decoration rect the
        // delegate recorded at paint time (checkbox excluded), so the hit test
        // matches the pixels exactly and cannot flicker against a re-derived guess.
        bool overIcon = false;
        if (it && !src.isNull()) {
          const auto* del = static_cast<ProjectRowDelegate*>(list_->itemDelegate());
          const QRect dec = del ? del->iconRectFor(list_->row(it)) : QRect();
          overIcon = dec.isValid() && dec.adjusted(-2, -2, 2, 2).contains(vpos);
        }
        // The magnifiable thumb advertises itself (browser .project-thumb img
        // cursor:zoom-in); back to the default arrow the moment the pointer leaves it.
        if (overIcon != hoverZoomCursor_) {
          if (overIcon) list_->viewport()->setCursor(zoomInCursor());
          else list_->viewport()->unsetCursor();
          hoverZoomCursor_ = overIcon;
        }
        if (overIcon) {
          // An APPEARANCE (first show, or a swap onto a different row) forms out of
          // the row and anchors there; moves that stay on the same thumb keep the
          // preview's dust alone (it replayed per move before, scattering motes with
          // every pixel of travel) but still carry the box along with
          // the pointer, browser positionZoom parity.
          const QPoint cur = static_cast<QMouseEvent*>(ev)->globalPosition().toPoint();
          const bool appearing = !hoverPreview_ || !hoverPreview_->isVisible() ||
                                 hoverClosing_ || hoverItem_ != it;
          if (appearing) {
            // A different row's preview still up? Dust it back into ITS row first —
            // the browser's old-thumb mouseleave plays surfaceOut before the new
            // thumb's mouseenter gathers.
            if (hoverPreview_ && hoverPreview_->isVisible() && hoverItem_ && hoverItem_ != it)
              hideHoverPreview();
            if (!hoverPreview_) {
              // Input-transparent, like the browser zoom's pointer-events:none — the
              // clamped glance (the doubled Alt one especially) can land UNDER the
              // pointer, and a window that eats the hover makes the viewport churn
              // Leave/Enter, replaying the dust on every move.
              hoverPreview_ = new QLabel(this, Qt::ToolTip | Qt::FramelessWindowHint |
                                                   Qt::WindowTransparentForInput |
                                                   Qt::WindowDoesNotAcceptFocus);
              hoverPreview_->setAttribute(Qt::WA_ShowWithoutActivating, true);
              hoverPreview_->setStyleSheet(
                  "QLabel{background:#1e1e1e;border:2px solid #d4a017;"
                  "border-radius:8px;padding:4px;}");
            }
            // Alt HELD magnifies the glance (chat HoverPreview / browser parity). The
            // SOURCE rides along so the Alt toggle below can re-scale without a move.
            const int edge =
                (QGuiApplication::queryKeyboardModifiers() & Qt::AltModifier)
                    ? kHoverPreviewAltPx
                    : kHoverPreviewPx;
            hoverPreview_->setProperty("srcPixmap", src);
            hoverPreview_->setPixmap(
                src.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            hoverPreview_->adjustSize();
            placeHoverPreview(cur);
            // Shown, but held invisible behind its gathering motes (revealHoverPreview
            // ramps it up); reduced motion shows the end state at once.
            hoverPreview_->setWindowOpacity(support::motionReduced() ? 1.0 : 0.0);
            hoverPreview_->show();
            hoverItem_ = it;
            revealHoverPreview(it);
          } else {
            placeHoverPreview(cur);
          }
        } else if (hoverPreview_) {
          hideHoverPreview();
        }
      } else if (ev->type() == QEvent::Leave) {
        setKebabHover(-1);   // …or the chip stays lit after the pointer has gone
        if (auto* tip = gui::appTooltip(); tip->owner() == list_->viewport()) tip->hideTip();
        // Same verified-against-the-cursor rule as the deactivate backstop above: a
        // Leave fired by our own preview window sliding under the pointer must not
        // hide what the pointer is still hovering.
        if (!pointerOverPreviewedIcon()) {
          if (hoverPreview_) hideHoverPreview();
          if (hoverZoomCursor_) {
            list_->viewport()->unsetCursor();
            hoverZoomCursor_ = false;
          }
        }
      }
    }
    return {};   // nothing here answered — the chain goes on
  }


}  // namespace stencil::gui
