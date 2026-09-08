// macOS body of installModalDismiss()'s outside-press watch (modalReveal.hpp).
//
// TWO cases, because Qt runs the two kinds of modal dialog through different AppKit paths:
//
//  • A WINDOW-modal dialog does not block the app's event stream, so the raw press behind
//    it still reaches a QAbstractNativeEventFilter — earlier than any QEvent, which QtGui
//    drops for a blocked window. MacModalDismiss below handles that.
//
//  • An APPLICATION-modal dialog (every Stencil dialog) runs under a Cocoa modal session,
//    which DISCARDS presses aimed at other, non-worksWhenModal windows before dispatch, so
//    neither a native filter nor a local NSEvent monitor sees them (verified). Only a
//    window in the session does, and a child of the modal window is worksWhenModal for
//    free — so a transparent child window under the dialog catches the press.
//
// Objective-C++ only to read the NSEvent and order the backdrop; the rest is plain Qt.
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
    // Shared gate: a visible dialog that may be clicked away — not a native/file panel, not
    // one that opted out (a question that must be answered).
    bool dismissable(QDialog* dlg) {
      return dlg && dlg->isVisible() && !qobject_cast<QFileDialog*>(dlg)
             && !dlg->property(kNoOutsideDismissProperty).toBool();
    }

    void rejectSoon(QDialog* dlg) {
      // Queued: the press is seen from inside AppKit's own send (a native filter) or a
      // popup grab, and rejecting a dialog out from under that re-enters the event loop.
      QPointer<QDialog> guard(dlg);
      QTimer::singleShot(0, dlg, [guard] { if (guard) guard->reject(); });
    }

    // ── Window-modal case: the raw NSEvent still reaches us ──────────────────────────
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
        // Inside the dialog's own frame — title bar included — is not an outside press.
        // Nor is a popup it raised: those are separate windows, so the geometry test is
        // what tells them apart. A press on a DIFFERENT window of ours is outside.
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

    // ── Application-modal case: a transparent backdrop under the dialog ──────────────
    constexpr const char* kBackdropAttachedProp = "stencilModalBackdropAttached";

    // Rejects its dialog on any press that reaches the backdrop.
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

    // Lays the backdrop under an application-modal dialog the first time it is shown.
    class BackdropWatcher : public QObject {
     protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::Show) attach(qobject_cast<QDialog*>(o));
        return QObject::eventFilter(o, e);
      }

     private:
      void attach(QDialog* dlg) {
        // Only a top-level, application-modal dialog reaches the broken AppKit path: a
        // window-modal one goes through the native filter above, a popover is not a window.
        if (!dlg || !dlg->isWindow() || dlg->windowModality() != Qt::ApplicationModal) return;
        if (!dismissable(dlg) || dlg->property(kBackdropAttachedProp).toBool()) return;
        // Offscreen has no real window server; the tests drive the Qt-level filter path.
        if (QGuiApplication::platformName() == QLatin1String("offscreen")) return;
        dlg->setProperty(kBackdropAttachedProp, true);

        // A child of the dialog, so it dies with it and is worksWhenModal for free;
        // frameless, translucent and never activating, so it shows nothing and takes no
        // focus. It spans every screen, since the dialog can be dragged to any of them.
        auto* backdrop = new QWidget(dlg, Qt::Tool | Qt::FramelessWindowHint
                                              | Qt::NoDropShadowWindowHint);
        backdrop->setObjectName(QStringLiteral("stencilModalBackdrop"));
        backdrop->setAttribute(Qt::WA_TranslucentBackground);
        backdrop->setAttribute(Qt::WA_NoSystemBackground);
        backdrop->setAttribute(Qt::WA_ShowWithoutActivating);
        QRect all;
        for (const QScreen* s : QGuiApplication::screens()) all |= s->geometry();
        backdrop->setGeometry(all);
        backdrop->installEventFilter(new BackdropPress(backdrop, dlg));
        backdrop->show();

        // BELOW the dialog, so the dialog and its popups stay interactive and only presses
        // that miss them reach the backdrop. Deferred so both native windows exist.
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
