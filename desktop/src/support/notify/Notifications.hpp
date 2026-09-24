#pragma once
#include "NotificationSink.hpp"
#include <QObject>
#include <QString>
#include <memory>

class QWidget;

// MainWindow's one notice channel: every notice comes through here and lands on the toast stack,
// or on the OS while the stored channel says so and the OS accepts. Browser twin:
// js/ui/shell/notifications.js notify().
namespace stencil::gui {

  class ToastStack;

  class Notifications : public QObject {
    Q_OBJECT
   public:
    using Level = NotifyLevel;

    explicit Notifications(QWidget* host);
    ~Notifications() override;

    void setChannel(NotifyChannel channel);
    NotifyChannel channel() const { return current; }
    bool isSystemAvailable() const { return system && system->isAvailable(); }
    // Tests: a stand-in for the OS sink.
    void setSystemSink(std::unique_ptr<NotificationSink> sink);
    ToastStack* toasts() const { return toast; }

    void info(const QString& text) { show(text, Level::INFO); }
    void success(const QString& text) { show(text, Level::SUCCESS); }
    void error(const QString& text) { show(text, Level::ERROR); }
    void show(const QString& text, Level level, int msec = 3000, bool special = false);
    // False when the OS did not take it — the toast channel, or a sink that declined — so a
    // caller with its own in-app surface (the chat toast) shows that instead.
    bool showSystem(const Notice& notice);

   private:
    ToastStack* toast = nullptr;
    std::unique_ptr<NotificationSink> system;
    NotifyChannel current = NotifyChannel::TOAST;
  };

}  // namespace stencil::gui
