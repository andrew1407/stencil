#include "AppTooltip.hpp"

namespace stencil::gui {

  AppTooltip* AppTooltipFilter::tip() {
    if (!tip_) tip_ = new AppTooltip(nullptr);
    return tip_;
  }

  bool AppTooltipFilter::eventFilter(QObject* o, QEvent* e) {
    auto* w = qobject_cast<QWidget*>(o);
    switch (e->type()) {
      case QEvent::ToolTip: {
        // Only a widget carrying its OWN tooltip; item views keep Qt's path.
        if (!w || w->toolTip().isEmpty()) break;
        const QPoint at = static_cast<QHelpEvent*>(e)->globalPos();
        // A control stretched across its row has its centre in empty space: wider than it
        // asked to be, the tip forms out of the cursor, as the browser's does.
        const bool stretched = w->width() > w->sizeHint().width() + 24;
        tip()->showFor(w, w->toolTip(), at,
                       stretched ? QRect(at - QPoint(4, 4), QSize(8, 8)) : QRect());
        // A control without a :hover rule gets no HoverMove, so tracking is borrowed for the
        // life of the tip only — left on, every widget keeps sending MouseMove for the session.
        if (!w->hasMouseTracking()) {
          w->setMouseTracking(true);
          tracked_ = w;
        }
        return true;   // Qt's own label must not also appear
      }
      case QEvent::Shortcut:
        // A LIVE shortcut never arrives as a key press — Qt sends this instead.
        dismiss();
        break;
      case QEvent::KeyPress: {
        const auto* ke = static_cast<QKeyEvent*>(e);
        if (ke->isAutoRepeat()) break;
        // Every key retires the tooltip, Escape included.
        if (tip_ && tip_->isVisible()) dismiss();
        break;
      }
      // A slide, not a re-show (showFor on every move stuttered). Styled controls get
      // HoverMove (QStyleSheetStyle sets WA_Hover); anything with tracking sends MouseMove.
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


  void AppTooltipFilter::dismiss() {
    if (tip_) tip_->hideTip();
    if (tracked_) tracked_->setMouseTracking(false);
    tracked_.clear();
  }


  // Idempotent — the filter and the panel are process-wide, like Qt's own tooltip.
  AppTooltipFilter* installAppTooltips() {
    static QPointer<AppTooltipFilter> filter;
    if (!filter && qApp) {
      filter = new AppTooltipFilter(qApp);
      qApp->installEventFilter(filter);
    }
    return filter.data();
  }


  AppTooltip* appTooltip() {
    AppTooltipFilter* f = installAppTooltips();
    return f ? f->tip() : nullptr;
  }
}  // namespace stencil::gui
