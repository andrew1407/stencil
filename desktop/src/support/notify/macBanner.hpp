#pragma once
#include <QString>
#include <functional>

// macOS banners through UNUserNotificationCenter. Qt's tray messages still post through the
// retired NSUserNotificationCenter, which macOS drops silently. Apple-only; SystemNotifier gates it.
namespace stencil::gui::macBanner {

  // A bundled app: a bare test binary has no bundle id and no notification centre.
  bool isSupported();
  // Asks the OS once; later calls only refresh the stored answer.
  void requestPermission();
  bool isAllowed();
  bool isDenied();
  // False until the OS has allowed banners; `onClick` runs on the main thread.
  bool post(const QString& title, const QString& body, std::function<void()> onClick);

}  // namespace stencil::gui::macBanner
