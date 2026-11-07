#include "AppTooltip.hpp"

namespace stencil::gui {

  namespace {
    // Enter reaches every ancestor too: only the innermost control with words of its own takes the tip.
    QWidget* carrierAt(const QPoint& global) {
      for (QWidget* x = QApplication::widgetAt(global); x; x = x->parentWidget())
        if (!x->toolTip().isEmpty()) return x;
      return nullptr;
    }
  }  // namespace

  AppTooltip* AppTooltipFilter::getTip() {
    if (!tip) {
      tip = new AppTooltip(nullptr);
      // Parentless, like Qt's own tip, so nothing else frees it: the event loop's end does.
      connect(qApp, &QCoreApplication::aboutToQuit, tip.data(), &QObject::deleteLater);
    }
    return tip;
  }

  bool AppTooltipFilter::eventFilter(QObject* o, QEvent* e) {
    auto* w = qobject_cast<QWidget*>(o);
    switch (e->type()) {
      case QEvent::ToolTip: {
        // Only a widget carrying its OWN tooltip; item views keep Qt's path.
        if (!w || w->toolTip().isEmpty()) break;
        showOn(w, static_cast<QHelpEvent*>(e)->globalPos());
        return true;   // Qt's own label must not also appear
      }
      // Mid-sweep (the tip up, its hide pending, or just gone) the next control takes it over at once:
      // Qt's own ToolTip would come only after the wake delay, with a fresh entrance.
      case QEvent::Enter:
        if (!tip || !w) break;
        if (w == tip->getOwner()) { if (leaveGrace) leaveGrace->stop(); break; }
        // A flying tip picture over the pointer carries nothing: then the control the pointer is inside takes it.
        if (!w->toolTip().isEmpty() && (tip->isRepointable() || (tip->isWarm() && w != left))
            && (carrierAt(QCursor::pos()) == w
                || (!carrierAt(QCursor::pos()) && w->rect().contains(w->mapFromGlobal(QCursor::pos()))))) {
          if (leaveGrace) leaveGrace->stop();
          showOn(w, QCursor::pos());
        }
        break;
      case QEvent::ToolTipChange:
        if (w) syncTipDescription(w);
        break;
      case QEvent::Shortcut:
        // A LIVE shortcut never arrives as a key press — Qt sends this instead.
        dismiss();
        break;
      case QEvent::KeyPress: {
        const auto* ke = static_cast<QKeyEvent*>(e);
        if (ke->isAutoRepeat()) break;
        // Every key retires the tooltip, Escape included.
        if (tip && tip->isVisible()) dismiss();
        break;
      }
      // A slide, not a re-show (showFor on every move stuttered). Styled controls get
      // HoverMove (QStyleSheetStyle sets WA_Hover); anything with tracking sends MouseMove.
      case QEvent::HoverMove:
        if (tip && w && w == tip->getOwner() && tip->isVisible())
          tip->moveTo(w->mapToGlobal(static_cast<QHoverEvent*>(e)->position().toPoint()));
        break;
      case QEvent::MouseMove:
        if (tip && w && w == tip->getOwner() && tip->isVisible())
          tip->moveTo(static_cast<QMouseEvent*>(e)->globalPosition().toPoint());
        break;
      case QEvent::Leave:
        if (!tip || !w || w != tip->getOwner()) break;
        if (!tip->isRepointable()) { dismiss(); break; }
        left = w;
        if (!leaveGrace) {
          leaveGrace = new QTimer(this);
          leaveGrace->setSingleShot(true);
          connect(leaveGrace, &QTimer::timeout, this, [this] { dismiss(); if (tip) tip->warmFor(AppTooltip::WARM_MS); });
        }
        leaveGrace->start(AppTooltip::LEAVE_GRACE_MS);
        break;
      case QEvent::Hide:
      case QEvent::WindowDeactivate:
        if (tip && w && w == tip->getOwner()) dismiss();
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


  // A control stretched across its row has its centre in empty space: wider than it asked to be, the
  // tip forms out of the cursor, as the browser's does.
  void AppTooltipFilter::showOn(QWidget* w, const QPoint& at) {
    const bool stretched = w->width() > w->sizeHint().width() + 24;
    getTip()->showFor(w, w->toolTip(), at, stretched ? QRect(at - QPoint(4, 4), QSize(8, 8)) : QRect());
    // A control without a :hover rule gets no HoverMove, so tracking is borrowed for the life of the tip
    // only — left on, every widget keeps sending MouseMove for the session.
    if (!w->hasMouseTracking()) {
      w->setMouseTracking(true);
      tracked = w;
    }
  }

  // Every dismissal ends a sweep: only the leave grace warms the next tip, after this.
  void AppTooltipFilter::dismiss() {
    if (leaveGrace) leaveGrace->stop();
    if (tip) { tip->hideTip(); tip->warmFor(0); }
    if (tracked) tracked->setMouseTracking(false);
    tracked.clear();
  }


  // Idempotent — the filter and the panel are process-wide, like Qt's own tooltip. The tips set
  // before it was listening get their plain reading once, here.
  AppTooltipFilter* installAppTooltips() {
    static QPointer<AppTooltipFilter> filter;
    if (!filter && qApp) {
      filter = new AppTooltipFilter(qApp);
      qApp->installEventFilter(filter);
      for (QWidget* w : QApplication::allWidgets()) syncTipDescription(w);
    }
    return filter.data();
  }


  AppTooltip* appTooltip() {
    AppTooltipFilter* f = installAppTooltips();
    return f ? f->getTip() : nullptr;
  }
}  // namespace stencil::gui
