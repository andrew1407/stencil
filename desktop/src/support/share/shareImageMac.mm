// macOS body of shareImage.hpp — NSSharingServicePicker. ARC-compiled (the one -fobjc-arc
// file). The picker shows asynchronously, so it lives in a static: under ARC that is
// already strong, and reassigning it releases the previous share.
#include "shareImage.hpp"

#include <QWidget>
#include <QWindow>
#import <AppKit/AppKit.h>

namespace stencil::support {

  namespace {
    NSSharingServicePicker* gPicker = nil;
  }

  bool isShareSheetAvailable() { return true; }

  bool showShareSheet(QWidget* anchor, const QString& filePath, const QString& title) {
    if (!anchor) return false;
    NSURL* url = [NSURL fileURLWithPath:filePath.toNSString()];
    if (!url) return false;
    // NSSharingServicePicker has no "subject": the FILE's name is what Mail shows, so
    // `title` is unused here and kept for parity with the other two platforms.
    (void)title;
    gPicker = [[NSSharingServicePicker alloc] initWithItems:@[ url ]];
    // The WINDOW's view, the button's rect within it: the button's own winId() made it and every
    // sibling native. Qt's views are flipped, so widget coordinates carry over as they are.
    QWidget* top = anchor->window();
    QWindow* handle = top ? top->windowHandle() : nullptr;
    NSView* view = handle ? (__bridge NSView*)reinterpret_cast<void*>(handle->winId()) : nil;
    if (!view) return false;
    const QPoint at = anchor->mapTo(top, QPoint(0, 0));
    [gPicker showRelativeToRect:NSMakeRect(at.x(), at.y(), anchor->width(), anchor->height())
                         ofView:view
                  preferredEdge:NSMinYEdge];
    return true;
  }

}  // namespace stencil::support
