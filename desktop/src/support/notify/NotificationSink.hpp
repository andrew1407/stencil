#pragma once
#include <QLatin1String>
#include <QString>
#include <functional>

// Where a notice is delivered: the in-app ToastStack or the OS (SystemNotifier). Notifications
// picks one by the stored channel. Browser twin: js/ui/shell/notifySinks.js.
namespace stencil::gui {

  enum class NotifyLevel { INFO, SUCCESS, ERROR };

  // The `notifyChannel` setting: "toast" (default) | "system".
  enum class NotifyChannel { TOAST, SYSTEM };

  struct Notice {
    QString text;
    NotifyLevel level = NotifyLevel::INFO;
    int msec = 3000;
    // A logo show's own notice: the egg on gold, whatever the accent.
    bool special = false;
    std::function<void()> onClick;
  };

  class NotificationSink {
   public:
    virtual ~NotificationSink() = default;
    // False when the sink cannot deliver right now, so the router falls back to the toasts.
    virtual bool show(const Notice& notice) = 0;
    // Whether this sink can deliver at all on this machine.
    virtual bool isAvailable() const = 0;
    // The channel now points here (or away): an OS sink shows or hides its tray icon.
    virtual void setActive(bool on) = 0;
  };

  inline NotifyChannel notifyChannelFromKey(const QString& key) {
    return key == QLatin1String("system") ? NotifyChannel::SYSTEM : NotifyChannel::TOAST;
  }
  inline QString notifyChannelKey(NotifyChannel channel) {
    return channel == NotifyChannel::SYSTEM ? QStringLiteral("system") : QStringLiteral("toast");
  }

}  // namespace stencil::gui
