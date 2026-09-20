// Headless check for toast coalescing (support/notifications): the same text requested twice quickly
// is ONE visible toast — the SAME widget, no entrance replay, its lifetime extended past the first
// timer's expiry — distinct texts still stack, and the coalesced toast still auto-dismisses.
#include "Notifications.hpp"
#include "toastShine.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLabel>
#include <QWidget>
#include <cstdio>
#include <functional>

using stencil::gui::Notifications;

#include "support/check.hpp"

static void pumpFor(int ms) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// Toasts still standing (the ones playing their exit carry the leaving flag).
static QList<QLabel*> standing(QWidget& host) {
  QList<QLabel*> out;
  for (QLabel* l : host.findChildren<QLabel*>("toast"))
    if (!l->property("stencilToastLeaving").toBool()) out.push_back(l);
  return out;
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QWidget host;
  host.resize(600, 400);
  host.show();
  Notifications notify(&host);

  // ── Same text twice quickly → one toast, same widget, lifetime extended.
  notify.show(QStringLiteral("Saved"), Notifications::Level::SUCCESS, 600);   // expires ~t=600
  pumpFor(50);
  QList<QLabel*> live = standing(host);
  check(live.size() == 1, "first toast appears");
  QLabel* first = live.isEmpty() ? nullptr : live.first();
  pumpFor(250);                                                               // t ≈ 300
  notify.show(QStringLiteral("Saved"), Notifications::Level::SUCCESS, 600);   // → expires ~t=900
  pumpFor(50);
  live = standing(host);
  check(live.size() == 1, "identical text does not stack a second toast");
  check(!live.isEmpty() && live.first() == first, "the standing toast is reused (no replay)");
  // Without the refresh the first timer would have fired at ~t=600; the coalesced
  // request restarted the clock, so at ~t=750 the toast still stands.
  pumpFor(400);                                                               // t ≈ 750
  live = standing(host);
  check(live.size() == 1 && live.first() == first,
        "lifetime was extended past the first timer's expiry");
  // …and it still dismisses once the extended lifetime runs out (~t=900).
  pumpFor(500);                                                               // t ≈ 1250
  check(standing(host).isEmpty(), "coalesced toast auto-dismisses after the extension");
  pumpFor(300);  // let the exit animation finish and delete the label

  // ── Distinct texts still stack.
  notify.show(QStringLiteral("Saved"), Notifications::Level::SUCCESS, 800);
  notify.show(QStringLiteral("Synced to file"), Notifications::Level::INFO, 800);
  pumpFor(50);
  check(standing(host).size() == 2, "distinct texts stack as separate toasts");

  // ── The golden shining tracks the pill's OWN geometry, not a clock: a window resize moves the
  // stack, and polling left the halo a frame behind at the pill's last place.
  {
    QWidget shineHost;
    shineHost.resize(400, 200);
    shineHost.show();
    auto* pill = new QLabel(QStringLiteral("Secret activated"), &shineHost);
    pill->setObjectName(QStringLiteral("toast"));
    pill->setGeometry(6, 160, 140, 28);
    pill->show();
    stencil::support::installToastShine(pill);
    QWidget* halo = shineHost.findChild<QWidget*>(QStringLiteral("toastShine"));
    check(halo, "the notice wears a halo");
    constexpr int M = stencil::support::ToastShine::MARGIN;
    const auto pinned = [&] { return halo->geometry() == pill->geometry().adjusted(-M, -M, M, M); };
    check(pinned(), "it starts on the pill");
    for (int dy : {40, -90, 25}) {
      pill->move(pill->x() + 5, pill->y() + dy);   // no event loop between: a poll would lag here
      check(pinned(), "…and it moves WITH the pill, in the same beat");
    }
    pill->resize(200, 36);
    check(pinned(), "…and grows with it");
  }

  std::printf("%s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}
