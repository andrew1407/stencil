#pragma once
#include "NotificationSink.hpp"
#include <QObject>
#include <functional>

class QSystemTrayIcon;

// The OS sink: QSystemTrayIcon::showMessage, the one notification centre Qt reaches on every
// platform. The tray icon exists only while the channel points here. Browser twin: SystemSink.
namespace stencil::gui {

  class SystemNotifier : public QObject, public NotificationSink {
    Q_OBJECT
   public:
    explicit SystemNotifier(QObject* parent = nullptr);

    bool isAvailable() const override;

    bool show(const Notice& notice) override;
    void setActive(bool on) override;

   private:
    QSystemTrayIcon* tray = nullptr;
    // The newest notice's action; the OS reports a click without saying on which.
    std::function<void()> onClick;
  };

}  // namespace stencil::gui
