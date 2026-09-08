// macOS body of installModalDismiss()'s outside-press watch (modalReveal.hpp).
//
// The Qt-level filter there sees nothing for the press we care about: a window a modal
// blocks never gets one. QtGui drops it in QGuiApplicationPrivate::processMouseEvent
// long before any QWidget event exists, so an application event filter — which runs on
// QEvent, further down still — is never reached. (A QTest::mouseClick posts straight to
// the widget and skips that check, which is why a test alone cannot prove this works.)
//
// A native event filter runs EARLIER than all of it, on the raw NSEvent, so it sees the
// press whatever Qt does with it afterwards. Objective-C++ only to read the event's type
// and screen point; the decision and the dismissal are plain Qt.
#include "modalReveal.hpp"

#include <QApplication>
#include <QDialog>
#include <QFileDialog>
#include <QPointer>
#include <QRect>
#include <QTimer>
#include <QAbstractNativeEventFilter>

#import <AppKit/AppKit.h>

namespace stencil::support {

  namespace {
    class MacModalDismiss : public QAbstractNativeEventFilter {
     public:
      bool nativeEventFilter(const QByteArray& type, void* message, qintptr*) override {
        if (type != "mac_generic_NSEvent" || !message) return false;
        NSEvent* ev = (__bridge NSEvent*)message;   // ARC: the event stays AppKit-owned
        if (ev.type != NSEventTypeLeftMouseDown && ev.type != NSEventTypeRightMouseDown)
          return false;
        auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dlg || !dlg->isVisible() || qobject_cast<QFileDialog*>(dlg)
            || dlg->property(kNoOutsideDismissProperty).toBool())
          return false;
        // Cocoa screen coordinates are bottom-left origin; Qt's are top-left.
        const NSPoint p = ev.window ? [ev.window convertPointToScreen:ev.locationInWindow]
                                    : ev.locationInWindow;
        NSScreen* main = NSScreen.screens.firstObject;
        const double h = main ? NSMaxY(main.frame) : 0;
        const QPoint at(int(p.x), int(h - p.y));
        // Anything inside the dialog's own frame — its title bar included — is not an
        // outside press. Nor is a popup it raised: those are separate windows, so the
        // geometry test is what tells them apart, and a combo popup always overlaps the
        // dialog it belongs to. A press on a DIFFERENT window of ours is outside.
        if (dlg->frameGeometry().contains(at)) return false;
        for (QWidget* w : QApplication::topLevelWidgets())
          if (w != dlg && w->isVisible() && w->windowFlags() & Qt::Popup
              && w->frameGeometry().contains(at))
            return false;
        // Queued: this runs inside AppKit's own send, before Qt has processed the event
        // at all, and rejecting a dialog out from under that re-enters the event loop.
        QPointer<QDialog> guard(dlg);
        QTimer::singleShot(0, dlg, [guard] { if (guard) guard->reject(); });
        return true;   // swallowed, like the browser's overlay eating the click
      }
    };
  }  // namespace

  void installModalDismissNative() {
    static MacModalDismiss* filter = nullptr;
    if (filter || !qApp) return;
    filter = new MacModalDismiss();
    qApp->installNativeEventFilter(filter);
  }

}  // namespace stencil::support
