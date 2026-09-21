// The list-viewport link of the Projects dialog's eventFilter chain (order in ProjectsDialog.cpp).
#include "ProjectsDialog.hpp"
#include "ProjectRowDelegate.hpp"
#include "projectsRowChrome.hpp"
#include "ServerClient.hpp"
#include "../../support/motion/ShimmerOverlay.hpp"
#include <QApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QTimer>
#include <optional>
#include "ReorderableListWidget.hpp"
#include "ProjectDragZones.hpp"
#include "../../support/motion/scrollReveal.hpp"
#include "../../support/tip/AppTooltip.hpp"
namespace stencil::gui {

  std::optional<bool> ProjectsDialog::filterListViewport(QObject* obj, QEvent* ev) {
    if (list && obj == list->viewport()) {
      // Rows re-clamp + re-elide to the new viewport width.
      if (ev->type() == QEvent::Resize) list->doItemsLayout();
      // The kebab click is consumed so it does not also start a drag/selection.
      if (ev->type() == QEvent::ToolTip && hover.hoverPreview && hover.hoverPreview->isVisible())
        return true;
      if (ev->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(ev);
        // itemClicked/itemDoubleClicked carry no modifiers — remember the press's (and where it landed).
        press.pressMods = me->modifiers();
        const QPoint vpos = me->position().toPoint();
        press.pressPos = vpos;
        QListWidgetItem* it = list->itemAt(vpos);
        // A press on the checkbox strip only toggles the box: no row-open, no confirm.
        const QRect vr = it ? list->visualItemRect(it) : QRect();
        press.pressOnCheck = me->button() == Qt::LeftButton && it &&
                        QRect(vr.left(), vr.top(), 34, vr.height()).contains(vpos);
        if (me->button() == Qt::LeftButton && it &&
            !it->data(Qt::UserRole).isNull() &&
            kebabZone(list->visualItemRect(it)).contains(vpos)) {
          list->setCurrentItem(it);
          showRowMenu(it, me->globalPosition().toPoint());
          return true;
        }
      }
      if (ev->type() == QEvent::ToolTip) {
        // AppTooltip's filter skips item views by design, so rows resolve their tip here.
        auto* he = static_cast<QHelpEvent*>(ev);
        QListWidgetItem* it = list->itemAt(he->pos());
        // The "⋯" carries its OWN tip (browser projectsModal.js menuBtn.title).
        const bool onKebab = it && !it->data(Qt::UserRole).isNull() &&
                             kebabZone(list->visualItemRect(it)).contains(he->pos());
        const QString tip = !it ? QString()
                                : (onKebab ? KEBAB_TIP : it->toolTip());
        if (tip.isEmpty()) { gui::appTooltip()->hideTip(); return true; }
        // …forming out of the CURSOR, where the browser's tooltip flies from (ui/tooltip.js dust).
        hover.tipRowText = tip;
        gui::appTooltip()->showFor(list->viewport(), tip, he->globalPos(),
                                   QRect(he->globalPos(), QSize(1, 1)));
        return true;
      }
      if (ev->type() == QEvent::MouseMove) {
        const QPoint vpos = static_cast<QMouseEvent*>(ev)->position().toPoint();
        QListWidgetItem* it = list->itemAt(vpos);
        const bool onKebab = it && !it->data(Qt::UserRole).isNull() &&
                             kebabZone(list->visualItemRect(it)).contains(vpos);
        setKebabHover(onKebab ? list->row(it) : -1);
        // While up, the tip SLIDES with the pointer: showFor per move re-ran its appearance and stuttered.
        if (auto* tip = gui::appTooltip(); tip->isVisible() && tip->getOwner() == list->viewport()) {
          const QString text = !it ? QString() : (onKebab ? KEBAB_TIP : it->toolTip());
          if (text.isEmpty() || text != hover.tipRowText) tip->hideTip();
          else tip->moveTo(static_cast<QMouseEvent*>(ev)->globalPosition().toPoint());
        }
        const QPixmap src = it ? it->data(Qt::UserRole + 2).value<QPixmap>() : QPixmap();
        // Only over the THUMBNAIL rect the delegate recorded at paint time, so the hit test cannot flicker.
        bool overIcon = false;
        if (it && !src.isNull()) {
          const auto* del = static_cast<ProjectRowDelegate*>(list->itemDelegate());
          const QRect dec = del ? del->iconRectFor(list->row(it)) : QRect();
          overIcon = dec.isValid() && dec.adjusted(-2, -2, 2, 2).contains(vpos);
        }
        if (overIcon != hover.hoverZoomCursor) {
          if (overIcon) list->viewport()->setCursor(zoomInCursor());
          else list->viewport()->unsetCursor();
          hover.hoverZoomCursor = overIcon;
        }
        if (overIcon) {
          // An APPEARANCE forms out of the row; a move on the same thumb keeps the dust alone but
          // carries the box along (browser positionZoom).
          const QPoint cur = static_cast<QMouseEvent*>(ev)->globalPosition().toPoint();
          const bool appearing = !hover.hoverPreview || !hover.hoverPreview->isVisible() ||
                                 hover.hoverClosing || hover.hoverItem != it;
          if (appearing) {
            // Dust a different row's preview back into ITS row first (browser surfaceOut before the new surfaceIn).
            if (hover.hoverPreview && hover.hoverPreview->isVisible() && hover.hoverItem && hover.hoverItem != it)
              hideHoverPreview();
            if (!hover.hoverPreview) {
              // Input-transparent (browser pointer-events:none): a glance under the pointer would churn Leave/Enter.
              hover.hoverPreview = new QLabel(this, Qt::ToolTip | Qt::FramelessWindowHint |
                                                   Qt::WindowTransparentForInput |
                                                   Qt::WindowDoesNotAcceptFocus);
              hover.hoverPreview->setAttribute(Qt::WA_ShowWithoutActivating, true);
              hover.hoverPreview->setStyleSheet(
                  "QLabel{background:#1e1e1e;border:2px solid #d4a017;"
                  "border-radius:8px;padding:4px;}");
            }
            // Alt HELD magnifies (browser parity); the SOURCE rides along so the Alt toggle can re-scale without a move.
            const int edge =
                (QGuiApplication::queryKeyboardModifiers() & Qt::AltModifier)
                    ? HOVER_PREVIEW_ALT_PX
                    : HOVER_PREVIEW_PX;
            hover.hoverPreview->setProperty("srcPixmap", src);
            hover.hoverPreview->setPixmap(
                src.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            hover.hoverPreview->adjustSize();
            placeHoverPreview(cur);
            // Held invisible behind its gathering motes; reduced motion shows the end state at once.
            hover.hoverPreview->setWindowOpacity(support::motionReduced() ? 1.0 : 0.0);
            hover.hoverPreview->show();
            hover.hoverItem = it;
            revealHoverPreview(it);
          } else {
            placeHoverPreview(cur);
          }
        } else if (hover.hoverPreview) {
          hideHoverPreview();
        }
      } else if (ev->type() == QEvent::Leave) {
        setKebabHover(-1);
        if (auto* tip = gui::appTooltip(); tip->getOwner() == list->viewport()) tip->hideTip();
        // A Leave fired by our own preview window sliding under the pointer must not hide what is still hovered.
        if (!pointerOverPreviewedIcon()) {
          if (hover.hoverPreview) hideHoverPreview();
          if (hover.hoverZoomCursor) {
            list->viewport()->unsetCursor();
            hover.hoverZoomCursor = false;
          }
        }
      }
    }
    return {};
  }


}  // namespace stencil::gui
