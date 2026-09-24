#include "Notifications.hpp"
#include "SystemNotifier.hpp"
#include "ToastStack.hpp"

namespace stencil::gui {

  Notifications::Notifications(QWidget* host) : QObject(host), toast(new ToastStack(host)) {
    auto os = std::make_unique<SystemNotifier>();
    os->onRefused = [this] {
      toast->show(tr("macOS does not allow notifications from Stencil — showing them in the app"),
                  Level::INFO, 5000);
    };
    system = std::move(os);
  }

  Notifications::~Notifications() = default;

  void Notifications::setChannel(NotifyChannel channel) {
    current = channel;
    if (system) system->setActive(channel == NotifyChannel::SYSTEM);
  }

  void Notifications::setSystemSink(std::unique_ptr<NotificationSink> sink) {
    if (system) system->setActive(false);
    system = std::move(sink);
    if (system) system->setActive(current == NotifyChannel::SYSTEM);
  }

  void Notifications::show(const QString& text, Level level, int msec, bool special) {
    if (showSystem({text, level, msec, special, {}})) return;
    toast->show(text, level, msec, special);
  }

  bool Notifications::showSystem(const Notice& notice) {
    return current == NotifyChannel::SYSTEM && system && system->show(notice);
  }

}  // namespace stencil::gui
