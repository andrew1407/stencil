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

    // NSSharingServicePicker has no "subject": the item itself is what a service shows.
    bool showPicker(QWidget* anchor, NSURL* item) {
      if (!anchor || !item) return false;
      // The WINDOW's view, the button's rect within it: the button's own winId() made it and every
      // sibling native. Qt's views are flipped, so widget coordinates carry over as they are.
      QWidget* top = anchor->window();
      QWindow* handle = top ? top->windowHandle() : nullptr;
      NSView* view = handle ? (__bridge NSView*)reinterpret_cast<void*>(handle->winId()) : nil;
      if (!view) return false;
      gPicker = [[NSSharingServicePicker alloc] initWithItems:@[ item ]];
      const QPoint at = anchor->mapTo(top, QPoint(0, 0));
      [gPicker showRelativeToRect:NSMakeRect(at.x(), at.y(), anchor->width(), anchor->height())
                           ofView:view
                    preferredEdge:NSMinYEdge];
      return true;
    }
  }  // namespace

  bool isShareSheetAvailable() { return true; }

  bool showShareSheet(QWidget* anchor, const QString& filePath, const QString& title) {
    (void)title;
    return showPicker(anchor, [NSURL fileURLWithPath:filePath.toNSString()]);
  }

  bool showShareLinkSheet(QWidget* anchor, const QString& url, const QString& title) {
    (void)title;
    return showPicker(anchor, [NSURL URLWithString:url.toNSString()]);
  }

}  // namespace stencil::support
