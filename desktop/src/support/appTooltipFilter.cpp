#include "appTooltip.hpp"

namespace stencil::gui {

  AppTooltip* AppTooltipFilter::tip() {
    if (!tip_) tip_ = new AppTooltip(nullptr);
    return tip_;
  }

  bool AppTooltipFilter::eventFilter(QObject* o, QEvent* e) {
    auto* w = qobject_cast<QWidget*>(o);
    switch (e->type()) {
      case QEvent::ToolTip: {
        // Only a widget carrying its OWN tooltip; anything else (item views resolving a
        // per-index tooltip in viewportEvent) keeps Qt's path.
        if (!w || w->toolTip().isEmpty()) break;
        const QPoint at = static_cast<QHelpEvent*>(e)->globalPos();
        // The tip forms out of the control it describes — its centre, which for a button
        // is the button. A control stretched across its row has its content at the left
        // and its centre in empty space, so wider than it asked to be forms out of the
        // cursor instead, as the browser's tooltip always does.
        const bool stretched = w->width() > w->sizeHint().width() + 24;
        tip()->showFor(w, w->toolTip(), at,
                       stretched ? QRect(at - QPoint(4, 4), QSize(8, 8)) : QRect());
        // The follow below needs moves: a control without a :hover rule gets no
        // HoverMove, and nothing sends MouseMove unpressed without tracking. Turned on
        // for the life of the tip only — left on, every widget that ever showed one
        // keeps sending MouseMove through this filter for the rest of the session.
        if (!w->hasMouseTracking()) {
          w->setMouseTracking(true);
          tracked_ = w;
        }
        return true;   // Qt's own label must not also appear
      }
      case QEvent::Shortcut:
        // A LIVE shortcut never arrives as a key press — Qt consumes the key and sends
        // this instead — so it has to be dismissed from here.
        dismiss();
        break;
      case QEvent::KeyPress: {
        const auto* ke = static_cast<QKeyEvent*>(e);
        if (ke->isAutoRepeat()) break;
        // Every key retires the tooltip, Escape included — the shake announces the
        // shortcut while you read the tip, it is not a way to pin the tooltip open.
        if (tip_ && tip_->isVisible()) dismiss();
        break;
      }
      // The tip travels with the pointer while it is up, as the browser's does: a slide,
      // not a re-show — showFor on every move re-ran the appearance and stuttered. Styled
      // controls get HoverMove (QStyleSheetStyle sets WA_Hover); anything with mouse
      // tracking sends MouseMove.
      case QEvent::HoverMove:
        if (tip_ && w && w == tip_->owner() && tip_->isVisible())
          tip_->moveTo(w->mapToGlobal(static_cast<QHoverEvent*>(e)->position().toPoint()));
        break;
      case QEvent::MouseMove:
        if (tip_ && w && w == tip_->owner() && tip_->isVisible())
          tip_->moveTo(static_cast<QMouseEvent*>(e)->globalPosition().toPoint());
        break;
      case QEvent::Leave:
      case QEvent::Hide:
      case QEvent::WindowDeactivate:
        if (tip_ && w && w == tip_->owner()) dismiss();
        break;
      case QEvent::MouseButtonPress:
      case QEvent::Wheel:
        dismiss();
        break;
      default:
        break;
    }
    return QObject::eventFilter(o, e);
  }


  // Hide the tip and hand back whatever mouse tracking it borrowed to follow the cursor.
  void AppTooltipFilter::dismiss() {
    if (tip_) tip_->hideTip();
    if (tracked_) tracked_->setMouseTracking(false);
    tracked_.clear();
  }


  // Install the fading tooltip on the running application. Idempotent — the filter and
  // the panel are process-wide, like Qt's own tooltip.
  AppTooltipFilter* installAppTooltips() {
    static QPointer<AppTooltipFilter> filter;
    if (!filter && qApp) {
      filter = new AppTooltipFilter(qApp);
      qApp->installEventFilter(filter);
    }
    return filter.data();
  }


  // The live panel, creating it on demand; null only with no QApplication.
  AppTooltip* appTooltip() {
    AppTooltipFilter* f = installAppTooltips();
    return f ? f->tip() : nullptr;
  }
}  // namespace stencil::gui
