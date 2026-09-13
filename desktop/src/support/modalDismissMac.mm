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

    // Every screen, padded: a drag can outrun one re-anchor, and an uncovered strip is a
    // press the dialog never hears.
    QRect allScreensPadded() {
      QRect all;
      for (const QScreen* s : QGuiApplication::screens()) all |= s->geometry();
      return all.adjusted(-2000, -2000, 2000, 2000);
    }

    /* AppKit moves a CHILD window with its parent, so dragging the dialog dragged the
     * backdrop out from under the app and a click there stopped dismissing. Re-anchored on
     * every move, it keeps covering the screens wherever the dialog goes. */
    class BackdropAnchor : public QObject {
     public:
      BackdropAnchor(QDialog* dlg, QWidget* backdrop) : QObject(dlg), backdrop_(backdrop) {}

     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        if ((e->type() == QEvent::Move || e->type() == QEvent::Resize) && backdrop_)
          backdrop_->setGeometry(allScreensPadded());
        return QObject::eventFilter(o, e);
      }
      QPointer<QWidget> backdrop_;
    };

    class BackdropPress : public QObject {
     public:
      BackdropPress(QWidget* backdrop, QDialog* dlg) : QObject(backdrop), dlg_(dlg) {}

     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::MouseButtonPress && dismissable(dlg_)) {
          modalDismissLog(QStringLiteral("[modal] backdrop (app-modal) dismiss: %1")
                              .arg(QString::fromLatin1(dlg_->metaObject()->className())));
          rejectSoon(dlg_);
          return true;
        }
        return QObject::eventFilter(o, e);
      }
      QPointer<QDialog> dlg_;
    };

    class BackdropWatcher : public QObject {
     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::Show) attach(qobject_cast<QDialog*>(o));
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
        // never activating; spans every screen since the dialog can be dragged anywhere.
        auto* backdrop = new QWidget(dlg, Qt::Tool | Qt::FramelessWindowHint
                                              | Qt::NoDropShadowWindowHint);
        backdrop->setObjectName(QStringLiteral("stencilModalBackdrop"));
        backdrop->setAttribute(Qt::WA_TranslucentBackground);
        backdrop->setAttribute(Qt::WA_NoSystemBackground);
        backdrop->setAttribute(Qt::WA_ShowWithoutActivating);
        backdrop->setGeometry(allScreensPadded());
        backdrop->installEventFilter(new BackdropPress(backdrop, dlg));
        dlg->installEventFilter(new BackdropAnchor(dlg, backdrop));
        backdrop->show();

        // BELOW the dialog so it and its popups stay interactive. Deferred so both native windows exist.
        QPointer<QDialog> dlgP(dlg);
        QPointer<QWidget> bdP(backdrop);
        QTimer::singleShot(0, dlg, [dlgP, bdP] {
          if (!dlgP || !bdP) return;
          NSView* dv = (__bridge NSView*)reinterpret_cast<void*>(dlgP->winId());
          NSView* bv = (__bridge NSView*)reinterpret_cast<void*>(bdP->winId());
          if (dv.window && bv.window) [dv.window addChildWindow:bv.window ordered:NSWindowBelow];
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
