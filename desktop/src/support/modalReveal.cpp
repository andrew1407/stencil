#include "modalReveal.hpp"
#include "ModalBackdrop.hpp"
#include "DisintegrateOverlay.hpp"
#include <QEvent>

#include <QAbstractAnimation>
#include <QAbstractScrollArea>
#include <QLayout>
#include <QResizeEvent>
#include <QColorDialog>
#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QGraphicsOpacityEffect>
#include <QDialog>
#include <QEasingCurve>
#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QTextStream>
#include <QLabel>
#include <QParallelAnimationGroup>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QRect>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <memory>

namespace stencil::support {

  // One surface ceiling across the app: a dialog's cloud is a surface like any other.
  static_assert(DIALOG_DUST_MAX_CELLS == gui::DisintegrateOverlay::SURFACE_MAX_CELLS,
                "a dialog's mote budget is the shared surface ceiling");

  namespace {
    // Set once a call site or the watcher owns the dialog's flight.
    constexpr const char* REVEALED_PROPERTY = "stencilDialogRevealed";
    // Dialog clocks run 1.5x the shared surface clock; the close another 1.5x on top.
    constexpr int OPEN_MS = 450;
    constexpr int CLOSE_MS = 360 * 3 / 2;
    constexpr int DIALOG_DUST_IN_MS = gui::DisintegrateOverlay::SURFACE_IN_MS * 3 / 2;
    constexpr int DIALOG_DUST_OUT_MS = gui::DisintegrateOverlay::SURFACE_OUT_MS * 9 / 4;

    // Flight origin/target in GLOBAL coords: the icon, else a small box above the dialog.
    QRect originRect(QWidget* anchor, const QRect& target, const QRect& anchorRect = QRect()) {
      if (anchor && anchor->isVisible() && anchor->width() > 0 && anchor->height() > 0)
        return QRect(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
      if (anchorRect.isValid() && anchorRect.width() > 0 && anchorRect.height() > 0)
        return anchorRect;
      const QSize small(qMax(target.width() / 4, 40), qMax(target.height() / 4, 32));
      const int above = target.top() - qMax(48, target.height() / 3) - small.height();
      return QRect(QPoint(target.center().x() - small.width() / 2, above), small);
    }

    bool anchorOnScreen(QWidget* anchor, const QRect& anchorRect) {
      if (anchor && anchor->isVisible() && anchor->width() > 0 && anchor->height() > 0) return true;
      return anchorRect.isValid() && anchorRect.width() > 0 && anchorRect.height() > 0;
    }

    // Exit target once the opener is gone: the canvas. Browser twin: ui/base.js canvasHomeRect.
    QRect canvasHomeRect(QWidget* host) {
      QWidget* canvas =
          host ? host->findChild<QWidget*>(QStringLiteral("canvasViewport")) : nullptr;
      if (!canvas || !canvas->isVisible() || canvas->width() < 1 || canvas->height() < 1)
        return QRect();
      constexpr int HOME_PX = 40;   // a small box, so the shrink reads as collapsing INTO it
      const QRect g(canvas->mapToGlobal(QPoint(0, 0)), canvas->size());
      return QRect(g.center() - QPoint(HOME_PX / 2, HOME_PX / 2), QSize(HOME_PX, HOME_PX));
    }

    // A CHILD of the main window, never a top-level: per-frame moves of a real window go
    // through the window server and stutter, and a snapshot has no layout to fight.
    QLabel* makeGhost(QWidget* host, const QPixmap& shot, const QRect& globalAt) {
      auto* ghost = new QLabel(host);
      ghost->setObjectName(QStringLiteral("stencilModalGhost"));  // findable by the GUI tests
      ghost->setAttribute(Qt::WA_TransparentForMouseEvents);
      ghost->setScaledContents(true);
      ghost->setPixmap(shot);
      ghost->setGeometry(QRect(host->mapFromGlobal(globalAt.topLeft()), globalAt.size()));
      ghost->raise();
      ghost->show();
      return ghost;
    }

    // `hold` keeps the box solid while small so the eye follows a window, not a fade.
    void flyGhost(QLabel* ghost, QWidget* host, const QRect& fromGlobal, const QRect& toGlobal,
                  int ms, double fromOpacity, double toOpacity, double hold,
                  QEasingCurve::Type easing, std::function<void()> done) {
      const QRect from(host->mapFromGlobal(fromGlobal.topLeft()), fromGlobal.size());
      const QRect to(host->mapFromGlobal(toGlobal.topLeft()), toGlobal.size());
      auto* fx = new QGraphicsOpacityEffect(ghost);
      fx->setOpacity(fromOpacity);
      ghost->setGraphicsEffect(fx);

      auto* geo = new QPropertyAnimation(ghost, "geometry", ghost);
      geo->setDuration(ms);
      geo->setStartValue(from);
      geo->setEndValue(to);
      geo->setEasingCurve(easing);
      auto* fade = new QPropertyAnimation(fx, "opacity", ghost);
      fade->setDuration(ms);
      fade->setKeyValueAt(0.0, fromOpacity);
      fade->setKeyValueAt(hold, qMax(fromOpacity, toOpacity));
      fade->setKeyValueAt(1.0, toOpacity);

      auto* group = new QParallelAnimationGroup(ghost);
      group->addAnimation(geo);
      group->addAnimation(fade);
      QObject::connect(group, &QParallelAnimationGroup::finished, ghost, [ghost, done] {
        if (done) done();
        ghost->deleteLater();
      });
      group->start(QAbstractAnimation::DeleteWhenStopped);
    }

    // Browser twin: js/ui/motion.js surfaceIn / surfaceOut. The ghost stays as the
    // fallback for anything the dust declines, so a window never simply blinks.
    bool flySurfaceDust(QWidget* host, const QPixmap& shot, const QRect& windowGlobal,
                        const QRect& iconGlobal, bool opening, const QColor& ink) {
      if (!host || shot.isNull() || !windowGlobal.isValid()) return false;
      const QRect box(host->mapFromGlobal(windowGlobal.topLeft()), windowGlobal.size());
      const QPoint point = host->mapFromGlobal(iconGlobal.center());
      auto* fx = gui::DisintegrateOverlay::overSurface(shot, box, host, point, opening,
                                                       opening ? DIALOG_DUST_IN_MS : DIALOG_DUST_OUT_MS, ink,
                                                       DIALOG_DUST_MAX_CELLS,
                                                       /*escapeHost=*/true);
      // Painted NOW: the dialog unmaps this turn, and one deferred frame is the blink.
      if (fx && !opening) fx->repaint();
      return fx != nullptr;
    }

    // Motes are lifted towards the window's text colour (DisintegrateOverlay::SURFACE_INK_MIX).
    QColor inkOf(const QWidget& w) { return w.palette().color(QPalette::WindowText); }

    void fadeUpBehindDust(QWidget* w) { gui::fadeUpBehindDust(w, DIALOG_DUST_IN_MS); }

    // Null only for an unparented dialog. Requiring the target to fit inside the host
    // silently turned the effect off for ordinary centred dialogs — do not add that.
    QWidget* hostFor(const QDialog& dlg) {
      QWidget* parent = dlg.parentWidget();
      if (!parent) return nullptr;
      QWidget* host = parent->window();
      return (host && host->isVisible()) ? host : nullptr;
    }
  }  // namespace

  void revealDialog(QDialog& dlg, QWidget* anchor) { revealDialog(dlg, anchor, QRect()); }

  // A scroll area decides its scrollbar on a posted layout pass that grab() runs ahead of.
  void settleLayout(QWidget& w) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    if (QLayout* l = w.layout()) l->activate();
    for (QAbstractScrollArea* area : w.findChildren<QAbstractScrollArea*>()) {
      QResizeEvent ev(area->size(), area->size());
      QCoreApplication::sendEvent(area, &ev);
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
  }

  namespace {
    void flyWindow(QWidget& w, QWidget* anchor, bool opening, std::function<void()> after) {
      QPointer<QWidget> guard(&w);
      QWidget* host = anchor ? anchor->window() : nullptr;
      const QRect target(w.mapToGlobal(QPoint(0, 0)), w.size());
      settleLayout(w);
      const QPixmap shot = w.grab();
      if (!host || !target.isValid() || shot.isNull()) { if (after) after(); return; }
      const QRect icon = originRect(anchor, target);
      if (icon == target) { if (after) after(); return; }
      const QRect from = opening ? icon : target;
      const QRect to = opening ? target : icon;
      if (opening) w.setWindowOpacity(0.0);
      if (flySurfaceDust(host, shot, target, icon, opening, inkOf(w))) {
        if (opening && guard) fadeUpBehindDust(guard);
        if (after) after();
        return;
      }
      QLabel* ghost = makeGhost(host, shot, from);
      flyGhost(ghost, host, from, to, opening ? OPEN_MS : CLOSE_MS,
               opening ? 0.0 : 1.0, opening ? 1.0 : 0.0, opening ? 0.18 : 0.7,
               opening ? QEasingCurve::OutCubic : QEasingCurve::InCubic,
               [guard, opening, after] {
                 if (guard && opening) guard->setWindowOpacity(1.0);
                 if (after) after();
               });
    }

    // Parented to the dialog; fires once — a dialog can be hidden again on its way to
    // destruction, and a second flight would fly an already-landed ghost.
    class CloseFlight : public QObject {
     public:
      CloseFlight(QDialog* dlg, QPointer<QWidget> anchor, QRect anchorRect,
                  std::shared_ptr<QPixmap> shot, QRect closeRect = QRect())
          : QObject(dlg), dlg_(dlg), anchor_(std::move(anchor)),
            anchorRect_(anchorRect), closeRect_(closeRect), shot_(std::move(shot)) {}

     protected:
      bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Hide && watched == dlg_ && !flown_) {
          flown_ = true;
          fly();
        }
        return QObject::eventFilter(watched, event);
      }

     private:
      void fly() {
        if (!dlg_) return;
        // Asked HERE, not at install: the settings dialog live-applies its own Motion rows.
        if (motionReduced()) return;
        const QRect target = dlg_->geometry();
        QWidget* host = hostFor(*dlg_);
        // Re-photograph NOW (grab() renders a hidden widget): the open-time shot is stale
        // once the content changed; it is only the fallback for a render that yields nothing.
        QPixmap shot = dlg_->grab();
        if (shot.isNull() && shot_) shot = *shot_;
        if (!host || !target.isValid() || shot.isNull()) return;
        QRect to = closeRect_.isValid() ? closeRect_
                                        : originRect(anchor_.data(), target, anchorRect_);
        if (!closeRect_.isValid() && !anchorOnScreen(anchor_.data(), anchorRect_)) {
          const QRect home = canvasHomeRect(host);
          if (home.isValid()) to = home;
        }
        if (to == target) return;
        if (flySurfaceDust(host, shot, target, to, false, inkOf(*dlg_))) return;
        QLabel* ghost = makeGhost(host, shot, target);
        // Painted NOW: one deferred frame is the gap the dialog's disappearance shows through.
        ghost->repaint();
        flyGhost(ghost, host, target, to, CLOSE_MS, 1.0, 0.0, 0.7,
                 QEasingCurve::InCubic, nullptr);
      }

      QPointer<QDialog> dlg_;
      QPointer<QWidget> anchor_;
      QRect anchorRect_;
      QRect closeRect_;
      std::shared_ptr<QPixmap> shot_;
      bool flown_ = false;
    };

    // Split from the public entry point so the watcher can play it WITHOUT claiming the dialog.
    void flyDialog(QDialog& dlg, QWidget* anchor, const QRect& anchorRect,
                   const QRect& closeRect = QRect()) {
      QPointer<QDialog> guard(&dlg);
      QPointer<QWidget> anchorGuard(anchor);
      auto shotWhileOpen = std::make_shared<QPixmap>();

      if (!motionReduced()) {
        // Transparent BEFORE exec() maps it, else the real window flashes at full size first.
        dlg.setWindowOpacity(0.0);
        const auto restore = [guard] { if (guard) guard->setWindowOpacity(1.0); };

        // 0-timer: geometry() is not the final box until exec() has laid the dialog out.
        QTimer::singleShot(0, &dlg, [guard, anchorGuard, anchorRect, restore, shotWhileOpen] {
          if (!guard || !guard->isVisible()) { restore(); return; }
          const QRect target(guard->mapToGlobal(QPoint(0, 0)), guard->size());
          QWidget* host = hostFor(*guard);
          settleLayout(*guard);   // the scrollbar in, before the photograph
          const QPixmap shot = guard->grab();
          *shotWhileOpen = shot;
          if (!host || !target.isValid() || shot.isNull()) { restore(); return; }
          const QRect from = originRect(anchorGuard.data(), target, anchorRect);
          if (from == target) { restore(); return; }
          if (flySurfaceDust(host, shot, target, from, true, inkOf(*guard))) { fadeUpBehindDust(guard); return; }
          QLabel* ghost = makeGhost(host, shot, from);
          flyGhost(ghost, host, from, target, OPEN_MS, 0.0, 1.0, 0.18,
                   QEasingCurve::OutCubic, restore);
        });
      }

      // Driven off the dialog's own Hide, NOT QDialog::finished: done() hides first and
      // emits a beat later, and in that gap the window server has already unmapped it.
      dlg.installEventFilter(new CloseFlight(&dlg, anchorGuard, anchorRect, shotWhileOpen, closeRect));
    }
  }  // namespace

  QColor pickColorAnimated(const QColor& initial, QWidget* parent, const QString& title,
                           QWidget* anchor, const QRect& anchorRect,
                           const std::function<void(const QColor&)>& preview, bool withAlpha,
                           const QRect& closeRect) {
    // Non-native: the macOS shared panel misbehaves under our event filters.
    QColorDialog dlg(parent);
    dlg.setOption(QColorDialog::DontUseNativeDialog);
    // Alpha only where the caller stores it: CSS `#rrggbbaa` (support/cssColor.hpp).
    if (withAlpha) dlg.setOption(QColorDialog::ShowAlphaChannel);
    dlg.setWindowTitle(title);
    dlg.setCurrentColor(initial);
    // exec() centres an unpositioned QDialog; revealDialog reads geometry after layout.
    if (preview) {
      QObject::connect(&dlg, &QColorDialog::currentColorChanged, &dlg,
                       [&preview](const QColor& c) { if (c.isValid()) preview(c); });
    }
    revealDialog(dlg, anchor, anchorRect, closeRect);
    const bool accepted = dlg.exec() == QDialog::Accepted;
    if (!accepted && preview) preview(initial);
    return accepted ? dlg.selectedColor() : QColor();
  }

  void revealWindow(QWidget& w, QWidget* anchor) {
    if (motionReduced()) return;
    flyWindow(w, anchor, true, nullptr);
  }

  void dismissWindow(QWidget& w, QWidget* anchor) {
    if (motionReduced()) { w.hide(); return; }
    QPointer<QWidget> guard(&w);
    flyWindow(w, anchor, false, nullptr);
    w.hide();
  }

  void revealDialog(QDialog& dlg, QWidget* anchor, const QRect& anchorRect) { revealDialog(dlg, anchor, anchorRect, QRect()); }
  void revealDialog(QDialog& dlg, QWidget* anchor, const QRect& anchorRect, const QRect& closeRect) {
    dlg.setProperty(REVEALED_PROPERTY, true);
    // A WINDOW dims and blurs what it covers; the popover flies itself and stays undimmed.
    ModalBackdrop::behind(&dlg, dlg.parentWidget() ? dlg.parentWidget()->window() : nullptr);
    flyDialog(dlg, anchor, anchorRect, closeRect);
  }

  namespace {
    // The browser's GESTURE_ANCHOR_PX, so a question forms out of the gesture on both surfaces.
    constexpr int GESTURE_ANCHOR_PX = 26;
    constexpr const char* DIALOG_REVEAL_FILTER_NAME = "stencilDialogRevealFilter";

    // One application-wide watcher: `QMessageBox::question(this, …)` has nowhere to hang a reveal off.
    class DialogRevealFilter : public QObject {
     public:
      explicit DialogRevealFilter(QObject* parent) : QObject(parent) {
        setObjectName(QString::fromLatin1(DIALOG_REVEAL_FILTER_NAME));
      }

     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() != QEvent::Show) return QObject::eventFilter(o, e);
        auto* dlg = qobject_cast<QDialog*>(o);
        auto* fileDlg = qobject_cast<QFileDialog*>(dlg);
        if (!dlg || dlg->property(REVEALED_PROPERTY).toBool()
            || dlg->property(NO_DIALOG_REVEAL_PROPERTY).toBool()
            // A NATIVE panel is placed by the OS; DontUseNativeDialog opts back in.
            || (fileDlg && !fileDlg->testOption(QFileDialog::DontUseNativeDialog)))
          return QObject::eventFilter(o, e);
        // A keyboard-raised question has no fresh point; a stale cursor is a gesture that never happened.
        flyDialog(*dlg, nullptr, gestureAnchorRect());
        return QObject::eventFilter(o, e);
      }
    };
  }  // namespace

  QRect gestureAnchorRect() {
    const QPoint p = QCursor::pos();
    return QRect(p.x() - GESTURE_ANCHOR_PX / 2, p.y() - GESTURE_ANCHOR_PX / 2,
                 GESTURE_ANCHOR_PX, GESTURE_ANCHOR_PX);
  }

  void modalDismissLog(const QString& line) {
    const QByteArray path = qgetenv("STENCIL_MODAL_LOG");
    if (path.isEmpty()) return;
    QFile f(QString::fromLocal8Bit(path));
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    QTextStream(&f) << line << '\n';
  }

  namespace {
    constexpr const char* MODAL_DISMISS_FILTER_NAME = "stencilModalDismissFilter";

    // Application-wide: the press goes to a BLOCKED window and QApplication drops it, but
    // an application filter still sees it first.
    class ModalDismissFilter : public QObject {
     public:
      explicit ModalDismissFilter(QObject* parent) : QObject(parent) {
        setObjectName(QString::fromLatin1(MODAL_DISMISS_FILTER_NAME));
      }

     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() != QEvent::MouseButtonPress) return QObject::eventFilter(o, e);
        auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* w = qobject_cast<QWidget*>(o);
        modalDismissLog(QStringLiteral("[modal] Qt press on %1, modal=%2")
                            .arg(QString::fromLatin1(w ? w->metaObject()->className()
                                                       : o->metaObject()->className()),
                                 QString::fromLatin1(dlg ? dlg->metaObject()->className() : "(none)")));
        if (!dlg || !w || !dlg->isVisible() || qobject_cast<QFileDialog*>(dlg)
            || dlg->property(NO_OUTSIDE_DISMISS_PROPERTY).toBool())
          return QObject::eventFilter(o, e);
        // Popups keep the widget they were built from as parent, so the walk reaches the dialog.
        for (const QWidget* p = w; p; p = p->parentWidget())
          if (p == dlg) return QObject::eventFilter(o, e);
        dlg->reject();
        return true;   // swallowed, like the overlay eating the click in the browser
      }
    };
  }  // namespace

  void installModalDismiss() {
    QCoreApplication* app = QCoreApplication::instance();
    if (!app) return;
    if (app->findChild<QObject*>(QString::fromLatin1(MODAL_DISMISS_FILTER_NAME),
                                 Qt::FindDirectChildrenOnly))
      return;
    app->installEventFilter(new ModalDismissFilter(app));
    installModalDismissNative();
  }

  // WEAK so headless binaries link without modalDismissMac.mm; MSVC has no weak symbols.
#if defined(_MSC_VER)
  void installModalDismissNative() {}
#else
  __attribute__((weak)) void installModalDismissNative() {}
#endif

  void installDialogReveal() {
    QCoreApplication* app = QCoreApplication::instance();
    // Offscreen has no compositor for windowOpacity. Reduced motion is re-read per flight.
    if (!app || QGuiApplication::platformName() == QLatin1String("offscreen")) return;
    if (app->findChild<QObject*>(QString::fromLatin1(DIALOG_REVEAL_FILTER_NAME),
                                 Qt::FindDirectChildrenOnly))
      return;
    app->installEventFilter(new DialogRevealFilter(app));
  }

}  // namespace stencil::support
