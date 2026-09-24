// Headless check for the notice channel (support/notify/Notifications): every notice lands on the
// toast stack by default; on the system channel the OS sink takes it, and one that declines
// hands it back to the toasts, so no notice is ever lost.
#include "Notifications.hpp"
#include "ToastStack.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLabel>
#include <QStringList>
#include <QWidget>
#include <memory>

using stencil::gui::Notice;
using stencil::gui::NotificationSink;
using stencil::gui::Notifications;
using stencil::gui::NotifyChannel;
using stencil::gui::NotifyLevel;

#include "../../support/check.hpp"

static void pumpFor(int ms) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

static QStringList toastTexts(QWidget& host) {
  QStringList out;
  for (QLabel* l : host.findChildren<QLabel*>("toast"))
    if (!l->property("stencilToastLeaving").toBool()) out << l->property("stencilToastText").toString();
  return out;
}

// A stand-in for the OS: records what it was handed, and can refuse.
struct FakeSink : NotificationSink {
  QStringList shown;
  QList<NotifyLevel> levels;
  bool accept = true;
  int active = -1;
  std::function<void()> lastClick;
  bool show(const Notice& notice) override {
    if (!accept) return false;
    shown << notice.text;
    levels << notice.level;
    lastClick = notice.onClick;
    return true;
  }
  bool isAvailable() const override { return true; }
  void setActive(bool on) override { active = on ? 1 : 0; }
};

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QWidget host;
  host.resize(600, 400);
  host.show();
  Notifications notify(&host);
  auto owned = std::make_unique<FakeSink>();
  FakeSink* fake = owned.get();
  notify.setSystemSink(std::move(owned));

  check(notify.channel() == NotifyChannel::TOAST, "the channel defaults to the in-app toasts");
  check(fake->active == 0, "installing a sink under the toast channel leaves it inactive");
  notify.success(QStringLiteral("Saved"));
  pumpFor(50);
  check(toastTexts(host) == QStringList{QStringLiteral("Saved")}, "toast channel: the stack shows it");
  check(fake->shown.isEmpty(), "toast channel: the OS sink hears nothing");

  notify.setChannel(NotifyChannel::SYSTEM);
  check(fake->active == 1, "the system channel activates the OS sink");
  notify.error(QStringLiteral("Could not save"));
  pumpFor(50);
  check(fake->shown == QStringList{QStringLiteral("Could not save")}, "system channel: the OS sink shows it");
  check(fake->levels.last() == NotifyLevel::ERROR, "…at the level it was raised");
  check(toastTexts(host) == QStringList{QStringLiteral("Saved")}, "system channel: no new toast");

  int clicks = 0;
  const bool taken = notify.showSystem({QStringLiteral("Assistant finished"), NotifyLevel::INFO, 6000, false,
                                        [&clicks] { ++clicks; }});
  check(taken, "a notice with an action is handed to the OS sink");
  if (fake->lastClick) fake->lastClick();
  check(clicks == 1, "…and its action rides along");

  fake->accept = false;
  notify.info(QStringLiteral("Synced"));
  pumpFor(50);
  check(toastTexts(host).contains(QStringLiteral("Synced")), "a declining OS sink falls back to the toasts");
  check(!notify.showSystem({QStringLiteral("x"), NotifyLevel::INFO, 1000, false, {}}),
        "showSystem reports the decline, so a caller may show its own surface");

  notify.setChannel(NotifyChannel::TOAST);
  check(fake->active == 0, "back on the toast channel the OS sink is deactivated");
  fake->accept = true;
  notify.info(QStringLiteral("Connected"));
  pumpFor(50);
  check(toastTexts(host).contains(QStringLiteral("Connected")), "toast channel again: the stack shows it");
  check(fake->shown.size() == 2, "…and the OS sink is not asked");

  check(stencil::gui::notifyChannelFromKey(QStringLiteral("system")) == NotifyChannel::SYSTEM, "key: system");
  check(stencil::gui::notifyChannelFromKey(QStringLiteral("toast")) == NotifyChannel::TOAST, "key: toast");
  check(stencil::gui::notifyChannelFromKey(QStringLiteral("bogus")) == NotifyChannel::TOAST, "an unknown key reads as toast");
  check(stencil::gui::notifyChannelKey(NotifyChannel::SYSTEM) == QLatin1String("system"), "key round-trips");

  std::printf("%s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}
