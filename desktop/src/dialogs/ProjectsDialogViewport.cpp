// The list-viewport link of the Projects dialog's eventFilter chain (order in ProjectsDialog.cpp).
#include "ProjectsDialog.hpp"
#include "ProjectRowDelegate.hpp"
#include "projectsRowChrome.hpp"
#include "ServerClient.hpp"
#include "../support/ShimmerOverlay.hpp"
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
#include "../support/scrollReveal.hpp"
#include "../support/AppTooltip.hpp"
namespace stencil::gui {

  std::optional<bool> ProjectsDialog::filterListViewport(QObject* obj, QEvent* ev) {
    if (list_ && obj == list_->viewport()) {
      // Rows re-clamp + re-elide to the new viewport width.
      if (ev->type() == QEvent::Resize) list_->doItemsLayout();
      // The kebab click is consumed so it does not also start a drag/selection.
      if (ev->type() == QEvent::ToolTip && hover_.hoverPreview && hover_.hoverPreview->isVisible())
        return true;
      if (ev->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(ev);
        // itemClicked/itemDoubleClicked carry no modifiers — remember the press's (and where it landed).
        press_.pressMods = me->modifiers();
        const QPoint vpos = me->position().toPoint();
        press_.pressPos = vpos;
        QListWidgetItem* it = list_->itemAt(vpos);
        // A press on the checkbox strip only toggles the box: no row-open, no confirm.
        const QRect vr = it ? list_->visualItemRect(it) : QRect();
        press_.pressOnCheck = me->button() == Qt::LeftButton && it &&
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
        // AppTooltip's filter skips item views by design, so rows resolve their tip here.
        auto* he = static_cast<QHelpEvent*>(ev);
        QListWidgetItem* it = list_->itemAt(he->pos());
        // The "⋯" carries its OWN tip (browser projectsModal.js menuBtn.title).
        const bool onKebab = it && !it->data(Qt::UserRole).isNull() &&
                             kebabZone(list_->visualItemRect(it)).contains(he->pos());
        const QString tip = !it ? QString()
                                : (onKebab ? KEBAB_TIP : it->toolTip());
        if (tip.isEmpty()) { gui::appTooltip()->hideTip(); return true; }
        // …forming out of the CURSOR, where the browser's tooltip flies from (ui/tooltip.js dust).
        hover_.tipRowText = tip;
        gui::appTooltip()->showFor(list_->viewport(), tip, he->globalPos(),
                                   QRect(he->globalPos(), QSize(1, 1)));
        return true;
      }
      if (ev->type() == QEvent::MouseMove) {
        const QPoint vpos = static_cast<QMouseEvent*>(ev)->position().toPoint();
        QListWidgetItem* it = list_->itemAt(vpos);
        const bool onKebab = it && !it->data(Qt::UserRole).isNull() &&
                             kebabZone(list_->visualItemRect(it)).contains(vpos);
        setKebabHover(onKebab ? list_->row(it) : -1);
        // While up, the tip SLIDES with the pointer: showFor per move re-ran its appearance and stuttered.
        if (auto* tip = gui::appTooltip(); tip->isVisible() && tip->owner() == list_->viewport()) {
          const QString text = !it ? QString() : (onKebab ? KEBAB_TIP : it->toolTip());
          if (text.isEmpty() || text != hover_.tipRowText) tip->hideTip();
          else tip->moveTo(static_cast<QMouseEvent*>(ev)->globalPosition().toPoint());
        }
        const QPixmap src = it ? it->data(Qt::UserRole + 2).value<QPixmap>() : QPixmap();
        // Only over the THUMBNAIL rect the delegate recorded at paint time, so the hit test cannot flicker.
        bool overIcon = false;
        if (it && !src.isNull()) {
          const auto* del = static_cast<ProjectRowDelegate*>(list_->itemDelegate());
          const QRect dec = del ? del->iconRectFor(list_->row(it)) : QRect();
          overIcon = dec.isValid() && dec.adjusted(-2, -2, 2, 2).contains(vpos);
        }
        if (overIcon != hover_.hoverZoomCursor) {
          if (overIcon) list_->viewport()->setCursor(zoomInCursor());
          else list_->viewport()->unsetCursor();
          hover_.hoverZoomCursor = overIcon;
        }
        if (overIcon) {
          // An APPEARANCE forms out of the row; a move on the same thumb keeps the dust alone but
          // carries the box along (browser positionZoom).
          const QPoint cur = static_cast<QMouseEvent*>(ev)->globalPosition().toPoint();
          const bool appearing = !hover_.hoverPreview || !hover_.hoverPreview->isVisible() ||
                                 hover_.hoverClosing || hover_.hoverItem != it;
          if (appearing) {
            // Dust a different row's preview back into ITS row first (browser surfaceOut before the new surfaceIn).
            if (hover_.hoverPreview && hover_.hoverPreview->isVisible() && hover_.hoverItem && hover_.hoverItem != it)
              hideHoverPreview();
            if (!hover_.hoverPreview) {
              // Input-transparent (browser pointer-events:none): a glance under the pointer would churn Leave/Enter.
              hover_.hoverPreview = new QLabel(this, Qt::ToolTip | Qt::FramelessWindowHint |
                                                   Qt::WindowTransparentForInput |
                                                   Qt::WindowDoesNotAcceptFocus);
              hover_.hoverPreview->setAttribute(Qt::WA_ShowWithoutActivating, true);
              hover_.hoverPreview->setStyleSheet(
                  "QLabel{background:#1e1e1e;border:2px solid #d4a017;"
                  "border-radius:8px;padding:4px;}");
            }
            // Alt HELD magnifies (browser parity); the SOURCE rides along so the Alt toggle can re-scale without a move.
            const int edge =
                (QGuiApplication::queryKeyboardModifiers() & Qt::AltModifier)
                    ? HOVER_PREVIEW_ALT_PX
                    : HOVER_PREVIEW_PX;
            hover_.hoverPreview->setProperty("srcPixmap", src);
            hover_.hoverPreview->setPixmap(
                src.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            hover_.hoverPreview->adjustSize();
            placeHoverPreview(cur);
            // Held invisible behind its gathering motes; reduced motion shows the end state at once.
            hover_.hoverPreview->setWindowOpacity(support::motionReduced() ? 1.0 : 0.0);
            hover_.hoverPreview->show();
            hover_.hoverItem = it;
            revealHoverPreview(it);
          } else {
            placeHoverPreview(cur);
          }
        } else if (hover_.hoverPreview) {
          hideHoverPreview();
        }
      } else if (ev->type() == QEvent::Leave) {
        setKebabHover(-1);
        if (auto* tip = gui::appTooltip(); tip->owner() == list_->viewport()) tip->hideTip();
        // A Leave fired by our own preview window sliding under the pointer must not hide what is still hovered.
        if (!pointerOverPreviewedIcon()) {
          if (hover_.hoverPreview) hideHoverPreview();
          if (hover_.hoverZoomCursor) {
            list_->viewport()->unsetCursor();
            hover_.hoverZoomCursor = false;
          }
        }
      }
    }
    return {};
  }


}  // namespace stencil::gui
