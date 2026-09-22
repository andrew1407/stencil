#include "dragPasteboard.hpp"

#import <AppKit/AppKit.h>

#include <QString>
#include <mutex>

// The macOS drag pasteboard read straight (NSPasteboardNameDrag — the same object the drop
// Qt just delivered came from). Qt maps text/uri-list, text and a bitmap; public.html and the
// promised file a browser offers Finder are the two it never shows us. ARC is on for this file.

namespace stencil::support::pasteboard {

  namespace {
    std::mutex mu;
    // One promise at a time. `generation` retires the one a timed-out drop walked past, so a
    // late arrival is deleted instead of being taken for the next drag's picture.
    unsigned generation = 0;
    NSFilePromiseReceiver* offered = nil;
    NSString* landed = nil;

    NSPasteboard* dragPasteboard() {
      return NSApp ? [NSPasteboard pasteboardWithName:NSPasteboardNameDrag] : nil;
    }
  }  // namespace

  Offer read() {
    Offer out;
    NSPasteboard* pb = dragPasteboard();
    if (!pb) return out;
    abandon();

    NSData* html = [pb dataForType:NSPasteboardTypeHTML];
    if (html.length) out.html = QByteArray::fromNSData(html);
    for (NSURL* u in [pb readObjectsForClasses:@[ NSURL.class ] options:nil]) {
      NSString* s = u.absoluteString;
      if (s.length) out.urls << QString::fromNSString(s);
    }

    NSArray* receivers = [pb readObjectsForClasses:@[ NSFilePromiseReceiver.class ] options:nil];
    if (!receivers.count) return out;
    const std::lock_guard<std::mutex> lock(mu);
    offered = receivers.firstObject;
    out.promise = true;
    return out;
  }

  bool fetch(const QString& destDir) {
    NSFilePromiseReceiver* receiver = nil;
    unsigned mine = 0;
    {
      const std::lock_guard<std::mutex> lock(mu);
      receiver = offered;
      mine = generation;
    }
    if (!receiver || destDir.isEmpty()) return false;

    NSURL* dest = [NSURL fileURLWithPath:destDir.toNSString() isDirectory:YES];
    NSOperationQueue* queue = [[NSOperationQueue alloc] init];
    [receiver receivePromisedFilesAtDestination:dest
                                        options:@{}
                                 operationQueue:queue
                                         reader:^(NSURL* written, NSError* error) {
      if (error || !written) return;
      const std::lock_guard<std::mutex> lock(mu);
      // Late: the drop already chose another candidate, so take the bytes back off disk.
      if (mine != generation) {
        [NSFileManager.defaultManager removeItemAtURL:written error:nil];
        return;
      }
      landed = written.path;
    }];
    return true;
  }

  QString poll() {
    const std::lock_guard<std::mutex> lock(mu);
    return landed ? QString::fromNSString(landed) : QString();
  }

  void abandon() {
    const std::lock_guard<std::mutex> lock(mu);
    ++generation;
    offered = nil;
    landed = nil;
  }

}  // namespace stencil::support::pasteboard
