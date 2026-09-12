// macOS body of shareImage.hpp — NSSharingServicePicker. ARC-compiled (the one -fobjc-arc
// file). The picker shows asynchronously, so it lives in a static: under ARC that is
// already strong, and reassigning it releases the previous share.
#include "shareImage.hpp"

#include <QWidget>
#import <AppKit/AppKit.h>

namespace stencil::support {

  namespace {
    NSSharingServicePicker* gPicker = nil;
  }

  bool shareSheetAvailable() { return true; }

  bool showShareSheet(QWidget* anchor, const QString& filePath, const QString& title) {
    if (!anchor) return false;
    NSURL* url = [NSURL fileURLWithPath:filePath.toNSString()];
    if (!url) return false;
    // NSSharingServicePicker has no "subject": the FILE's name is what Mail shows, so
    // `title` is unused here and kept for parity with the other two platforms.
    (void)title;
    gPicker = [[NSSharingServicePicker alloc] initWithItems:@[ url ]];
    // winId() is the NSView* on macOS; a __bridge cast, not reinterpret_cast, tells ARC
    // the pointer conveys no ownership.
    NSView* view = (__bridge NSView*)reinterpret_cast<void*>(anchor->winId());
    if (!view) return false;
    [gPicker showRelativeToRect:view.bounds ofView:view preferredEdge:NSMinYEdge];
    return true;
  }

}  // namespace stencil::support
