// macOS body of installModalDismiss() (modalReveal.hpp). A WINDOW-modal dialog's raw
// press still reaches a QAbstractNativeEventFilter; an APPLICATION-modal one runs under
// a Cocoa modal session that DISCARDS presses aimed at non-worksWhenModal windows, so a
// transparent child window under the dialog (worksWhenModal for free) catches them.
#include "modalReveal.hpp"

#include <QApplication>
#include <QDialog>
#include <QFileDialog>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPointer>
#include <QRect>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <QtGlobal>
#include <QAbstractNativeEventFilter>

#import <AppKit/AppKit.h>

namespace stencil::support {

  namespace {
    // A visible dialog that may be clicked away — not a native panel, not one that opted out.
    bool dismissable(QDialog* dlg) {
      return dlg && dlg->isVisible() && !qobject_cast<QFileDialog*>(dlg)
             && !dlg->property(NO_OUTSIDE_DISMISS_PROPERTY).toBool();
    }

    void rejectSoon(QDialog* dlg) {
      // Queued: rejecting a dialog from inside AppKit's own send re-enters the event loop.
      QPointer<QDialog> guard(dlg);
      QTimer::singleShot(0, dlg, [guard] { if (guard) guard->reject(); });
    }

    class MacModalDismiss : public QAbstractNativeEventFilter {
     public:
      bool nativeEventFilter(const QByteArray& type, void* message, qintptr*) override {
        if (type != "mac_generic_NSEvent" || !message) return false;
        NSEvent* ev = (__bridge NSEvent*)message;   // ARC: the event stays AppKit-owned
        if (ev.type != NSEventTypeLeftMouseDown && ev.type != NSEventTypeRightMouseDown)
          return false;
        auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dismissable(dlg)) return false;
        // Cocoa screen coordinates are bottom-left origin; Qt's are top-left.
        const NSPoint p = ev.window ? [ev.window convertPointToScreen:ev.locationInWindow]
                                    : ev.locationInWindow;
        NSScreen* main = NSScreen.screens.firstObject;
        const double h = main ? NSMaxY(main.frame) : 0;
        const QPoint at(int(p.x), int(h - p.y));
        // Inside the dialog's frame (title bar included) or a popup it raised is not outside.
        if (dlg->frameGeometry().contains(at)) return false;
        for (QWidget* w : QApplication::topLevelWidgets())
          if (w != dlg && w->isVisible() && w->windowFlags() & Qt::Popup
              && w->frameGeometry().contains(at))
            return false;
        modalDismissLog(QStringLiteral("[modal] native (window-modal) dismiss: %1")
                            .arg(QString::fromLatin1(dlg->metaObject()->className())));
        rejectSoon(dlg);
        return true;   // swallowed, like the browser's overlay eating the click
      }
    };

    constexpr const char* BACKDROP_ATTACHED_PROP = "stencilModalBackdropAttached";

    // The app's own window, as the browser's overlay covers only its page: a transparent window
    // over every screen was the first thing macOS brought back on a desktop switch, and blinked.
    QRect catcherRect(const QDialog* dlg) {
      const QWidget* host = dialogHost(dlg);
      if (host && host->isVisible()) return host->frameGeometry();
      QRect all;
      for (const QScreen* s : QGuiApplication::screens()) all |= s->availableGeometry();
      return all;
    }

    /* AppKit moves a CHILD window with its parent, so dragging the dialog dragged the
     * backdrop out from under the app and a click there stopped dismissing. Re-anchored on
     * every move, it keeps covering the app's window wherever the dialog goes. */
    class BackdropAnchor : public QObject {
     public:
      BackdropAnchor(QDialog* dlg, QWidget* backdrop) : QObject(dlg), dlg(dlg), backdrop(backdrop) {}

     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        if ((e->type() == QEvent::Move || e->type() == QEvent::Resize) && backdrop && dlg)
          backdrop->setGeometry(catcherRect(dlg));
        return QObject::eventFilter(o, e);
      }
      QPointer<QDialog> dlg;
      QPointer<QWidget> backdrop;
    };

    class BackdropPress : public QObject {
     public:
      BackdropPress(QWidget* backdrop, QDialog* dlg) : QObject(backdrop), dlg(dlg) {}

     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::MouseButtonPress && dismissable(dlg)) {
          modalDismissLog(QStringLiteral("[modal] backdrop (app-modal) dismiss: %1")
                              .arg(QString::fromLatin1(dlg->metaObject()->className())));
          rejectSoon(dlg);
          return true;
        }
        return QObject::eventFilter(o, e);
      }
      QPointer<QDialog> dlg;
    };

    // A window's NSView through its OWN QWindow: QWidget::winId() marks the widget native, and Qt
    // then made every child of its parent a native view too, each repainted on a desktop switch.
    NSView* nativeView(QWidget* w) {
      QWindow* handle = w && w->isWindow() ? w->windowHandle() : nullptr;
      return handle ? (__bridge NSView*)reinterpret_cast<void*>(handle->winId()) : nil;
    }

    // AppKit zooms and fades a newly ordered window in by itself. Under a still interface that
    // is a lag nobody asked for, and under a moving one the app's own flight already plays.
    void noAppearAnimation(QWidget* w) {
      if (!w || !w->isWindow() || QGuiApplication::platformName() == QLatin1String("offscreen")) return;
      NSView* v = nativeView(w);
      if (v.window) v.window.animationBehavior = NSWindowAnimationBehaviorNone;
    }

    // Qt makes a dialog a panel that moves to the active desktop, so a desktop switch dropped it out
    // of the sliding desktop and put it back after the slide: the blink. It stays with its window.
    void stayOnOwnDesktop(QWidget* w) {
      if (!w || !w->isWindow() || QGuiApplication::platformName() == QLatin1String("offscreen")) return;
      NSWindow* nw = nativeView(w).window;
      if (nw) nw.collectionBehavior = NSWindowCollectionBehaviorManaged
                                      | NSWindowCollectionBehaviorFullScreenAuxiliary;
    }

    class BackdropWatcher : public QObject {
     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::Show) {
          auto* dlg = qobject_cast<QDialog*>(o);
          noAppearAnimation(dlg);
          stayOnOwnDesktop(dlg);
          attach(dlg);
        }
        return QObject::eventFilter(o, e);
      }

     private:
      void attach(QDialog* dlg) {
        // Only a top-level, application-modal dialog reaches the broken AppKit path.
        if (!dlg || !dlg->isWindow() || dlg->windowModality() != Qt::ApplicationModal) return;
        if (!dismissable(dlg) || dlg->property(BACKDROP_ATTACHED_PROP).toBool()) return;
        // Offscreen has no real window server; the tests drive the Qt-level filter path.
        if (QGuiApplication::platformName() == QLatin1String("offscreen")) return;
        dlg->setProperty(BACKDROP_ATTACHED_PROP, true);

        // A child of the dialog (dies with it, worksWhenModal for free); frameless, translucent,
        // never activating; over the app's own window, re-anchored as the dialog is dragged.
        auto* backdrop = new QWidget(dlg, Qt::Tool | Qt::FramelessWindowHint
                                              | Qt::NoDropShadowWindowHint);
        backdrop->setObjectName(QStringLiteral("stencilModalBackdrop"));
        backdrop->setAttribute(Qt::WA_TranslucentBackground);
        backdrop->setAttribute(Qt::WA_NoSystemBackground);
        backdrop->setAttribute(Qt::WA_ShowWithoutActivating);
        backdrop->setGeometry(catcherRect(dlg));
        backdrop->installEventFilter(new BackdropPress(backdrop, dlg));
        dlg->installEventFilter(new BackdropAnchor(dlg, backdrop));
        backdrop->show();
        noAppearAnimation(backdrop);   // after show: before it, only winId() could reach the window
        stayOnOwnDesktop(backdrop);

        // BELOW the dialog so it and its popups stay interactive. Deferred so both native windows exist.
        QPointer<QDialog> dlgP(dlg);
        QPointer<QWidget> bdP(backdrop);
        QTimer::singleShot(0, dlg, [dlgP, bdP] {
          if (!dlgP || !bdP) return;
          NSView* dv = nativeView(dlgP);
          NSView* bv = nativeView(bdP);
          if (dv.window && bv.window) [dv.window addChildWindow:bv.window ordered:NSWindowBelow];
          // A tool panel leaves with the app and comes back on its own, re-stacked: switching away and
          // back flashed the screen. It stays put, and lets clicks through while another app is up.
          bv.window.hidesOnDeactivate = NO;
          QObject::connect(qApp, &QGuiApplication::applicationStateChanged, bdP,
                           [bdP](Qt::ApplicationState state) {
                             if (!bdP) return;
                             nativeView(bdP).window.ignoresMouseEvents = state != Qt::ApplicationActive;
                           });
        });
        modalDismissLog(QStringLiteral("[modal] backdrop attached to %1")
                            .arg(QString::fromLatin1(dlg->metaObject()->className())));
      }
    };
  }  // namespace

  void installModalDismissNative() {
    if (!qApp) return;
    static MacModalDismiss* filter = nullptr;
    if (!filter) {
      filter = new MacModalDismiss();
      qApp->installNativeEventFilter(filter);
    }
    static BackdropWatcher* watcher = nullptr;
    if (!watcher) {
      watcher = new BackdropWatcher();
      qApp->installEventFilter(watcher);
    }
    modalDismissLog(QStringLiteral("[modal] native filter + backdrop watcher installed"));
  }

}  // namespace stencil::support
