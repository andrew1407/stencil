// macOS body of macBanner.hpp — UNUserNotificationCenter, ARC-compiled. The OS answers on its
// own queues, so every answer hops to the main queue before it touches the stored state.
#include "macBanner.hpp"

#import <Foundation/Foundation.h>
#import <UserNotifications/UserNotifications.h>

namespace {
  enum class Status { UNKNOWN, ALLOWED, DENIED };
  Status gStatus = Status::UNKNOWN;
  std::function<void()> gOnClick;

  void store(UNAuthorizationStatus s) {
    const Status next = s == UNAuthorizationStatusDenied ? Status::DENIED
        : s == UNAuthorizationStatusNotDetermined        ? Status::UNKNOWN
                                                         : Status::ALLOWED;
    dispatch_async(dispatch_get_main_queue(), ^{ gStatus = next; });
  }
}  // namespace

@interface StencilBannerDelegate : NSObject <UNUserNotificationCenterDelegate>
@end

@implementation StencilBannerDelegate
// The frontmost app's own notices stay hidden unless the delegate asks for the banner.
- (void)userNotificationCenter:(UNUserNotificationCenter*)center
       willPresentNotification:(UNNotification*)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler {
  completionHandler(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionList);
}

- (void)userNotificationCenter:(UNUserNotificationCenter*)center
    didReceiveNotificationResponse:(UNNotificationResponse*)response
             withCompletionHandler:(void (^)(void))completionHandler {
  dispatch_async(dispatch_get_main_queue(), ^{
    if (gOnClick) gOnClick();
  });
  completionHandler();
}
@end

namespace stencil::gui::macBanner {

  namespace {
    // The centre holds its delegate weakly; this static keeps it alive.
    StencilBannerDelegate* gDelegate = nil;

    UNUserNotificationCenter* center() {
      UNUserNotificationCenter* c = UNUserNotificationCenter.currentNotificationCenter;
      if (!gDelegate) {
        gDelegate = [StencilBannerDelegate new];
        c.delegate = gDelegate;
      }
      return c;
    }

    void refresh() {
      [center() getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings* s) {
        store(s.authorizationStatus);
      }];
    }
  }  // namespace

  bool isSupported() { return NSBundle.mainBundle.bundleIdentifier != nil; }
  bool isAllowed() { return gStatus == Status::ALLOWED; }
  bool isDenied() { return gStatus == Status::DENIED; }

  void requestPermission() {
    if (!isSupported()) return;
    [center() requestAuthorizationWithOptions:UNAuthorizationOptionAlert
                            completionHandler:^(BOOL, NSError*) { refresh(); }];
  }

  bool post(const QString& title, const QString& body, std::function<void()> onClick) {
    if (!isSupported() || gStatus != Status::ALLOWED) return false;
    gOnClick = std::move(onClick);
    UNMutableNotificationContent* content = [UNMutableNotificationContent new];
    content.title = title.toNSString();
    content.body = body.toNSString();
    UNNotificationRequest* request =
        [UNNotificationRequest requestWithIdentifier:NSUUID.UUID.UUIDString content:content trigger:nil];
    [center() addNotificationRequest:request withCompletionHandler:nil];
    // A switch turned off in System Settings meanwhile reaches the next notice.
    refresh();
    return true;
  }

}  // namespace stencil::gui::macBanner
