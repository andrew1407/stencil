#include "SystemNotifier.hpp"
#include <QIcon>
#include <QSystemTrayIcon>
#include <map>
#ifdef Q_OS_MACOS
#include "macBanner.hpp"
#endif

namespace stencil::gui {

  namespace {
    // An OS banner clocks longer than a corner toast: 3000 reads as a flash there.
    constexpr int MIN_OS_MSEC = 5000;

    const std::map<NotifyLevel, QSystemTrayIcon::MessageIcon> LEVEL_ICONS = {
        {NotifyLevel::INFO, QSystemTrayIcon::Information},
        {NotifyLevel::SUCCESS, QSystemTrayIcon::Information},
        {NotifyLevel::ERROR, QSystemTrayIcon::Critical},
    };

    QSystemTrayIcon::MessageIcon iconFor(NotifyLevel level) {
      const auto it = LEVEL_ICONS.find(level);
      return it == LEVEL_ICONS.end() ? QSystemTrayIcon::Information : it->second;
    }

#ifdef Q_OS_MACOS
    bool usesBanner() { return macBanner::isSupported(); }
#endif
  }  // namespace

  SystemNotifier::SystemNotifier(QObject* parent) : QObject(parent) {}

  bool SystemNotifier::isAvailable() const {
#ifdef Q_OS_MACOS
    if (usesBanner()) return !macBanner::isDenied();
#endif
    return QSystemTrayIcon::isSystemTrayAvailable() && QSystemTrayIcon::supportsMessages();
  }

  void SystemNotifier::setActive(bool on) {
#ifdef Q_OS_MACOS
    if (usesBanner()) {
      active = on;
      if (on) macBanner::requestPermission();
      return;
    }
#endif
    if (on && !tray && isAvailable()) {
      tray = new QSystemTrayIcon(QIcon(QStringLiteral(":/icons/appicon.svg")), this);
      tray->setToolTip(QStringLiteral("Stencil"));
      connect(tray, &QSystemTrayIcon::messageClicked, this, [this] {
        if (onClick) onClick();
      });
    }
    if (tray) tray->setVisible(on);
  }

  bool SystemNotifier::show(const Notice& notice) {
#ifdef Q_OS_MACOS
    if (usesBanner())
      return active && macBanner::post(QStringLiteral("Stencil"), notice.text, notice.onClick);
#endif
    if (!tray || !tray->isVisible()) return false;
    onClick = notice.onClick;
    tray->showMessage(QStringLiteral("Stencil"), notice.text, iconFor(notice.level),
                      qMax(notice.msec, MIN_OS_MSEC));
    return true;
  }

}  // namespace stencil::gui
