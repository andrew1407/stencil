// macOS body of shareImage.hpp — NSSharingServicePicker, the same "AirDrop / Mail /
// Notes / Messages / any installed extension" sheet the Finder's own Share button
// shows. Objective-C++ so it can talk to AppKit directly; every other TU in the app
// only ever sees the plain C++ declaration in shareImage.hpp.
//
// ARC-compiled (desktop/CMakeLists.txt sets -fobjc-arc on this one file — nothing
// else in the app is Objective-C, so there is nothing else for that flag to touch).
// The picker has to outlive this function (it shows asynchronously), so it is kept
// in a static instead of a local: under ARC a plain `static … *` is already strong,
// and reassigning it releases whatever share was showing before a new one starts.
#include "shareImage.hpp"

#include <QWidget>
#import <AppKit/AppKit.h>

namespace stencil::support {

  namespace {
    NSSharingServicePicker* gPicker = nil;
  }

  bool showShareSheet(QWidget* anchor, const QString& filePath, const QString& title) {
    if (!anchor) return false;
    NSURL* url = [NSURL fileURLWithPath:filePath.toNSString()];
    if (!url) return false;
    // NSSharingServicePicker has no separate "subject" field of its own — the shared
    // FILE's own name is what Mail/Messages/etc. show, which is already the browser's
    // "<base>-drawing.png" convention. There is nothing to set `title` on here; it
    // stays a parameter for parity with the other two platforms, which do use it.
    (void)title;
    gPicker = [[NSSharingServicePicker alloc] initWithItems:@[ url ]];
    // Anchored to the control that was clicked, like every other popover in the app
    // (support/modalReveal.cpp). winId() is the NSView* itself on macOS (Qt just
    // opaques it through WId) — a __bridge cast, not reinterpret_cast, because ARC
    // has to be told this pointer conveys no ownership: the view is already owned
    // by the window, not by us.
    NSView* view = (__bridge NSView*)reinterpret_cast<void*>(anchor->winId());
    if (!view) return false;
    [gPicker showRelativeToRect:view.bounds ofView:view preferredEdge:NSMinYEdge];
    return true;
  }

}  // namespace stencil::support
