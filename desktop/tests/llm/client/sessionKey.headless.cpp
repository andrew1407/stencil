// The anthropic session key's holder (src/llm/client/SessionKey): the canon TTL on a fake clock, the
// expiry timer on a short TTL, forget, and the wipe when the application quits.
#include "SessionKey.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <cstdio>

#include "../../support/check.hpp"

using stencil::llm::SessionKey;

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  std::printf("ttl on a fake clock:\n");
  {
    const QDateTime t0(QDate(2026, 9, 27), QTime(9, 0));
    QDateTime now = t0;
    SessionKey held(0, [&now] { return now; });
    int changes = 0;
    QObject::connect(&held, &SessionKey::changed, [&changes] { ++changes; });
    check(held.key().isEmpty() && !held.expiresAt().isValid(), "nothing is held at first");
    const QDateTime until = held.hold(QStringLiteral("  sk-ant-test-0123456789abcdef \n"));
    check(until == t0.addSecs(720 * 60), "held for providers.json sessionKey.ttlMinutes (720)");
    check(held.key() == "sk-ant-test-0123456789abcdef", "the key is held trimmed");
    check(held.expiresAt() == until && changes == 1, "the expiry is reported and the hold announced");
    now = until.addMSecs(-1);
    check(held.key() == "sk-ant-test-0123456789abcdef", "still held a millisecond before the expiry");
    now = until;
    check(held.key().isEmpty(), "dropped at the expiry, before any request reads it");
    check(!held.expiresAt().isValid() && changes == 2, "the drop is announced once");
    check(held.key().isEmpty() && changes == 2, "a second read announces nothing");
  }

  std::printf("forget:\n");
  {
    SessionKey held;
    int changes = 0;
    QObject::connect(&held, &SessionKey::changed, [&changes] { ++changes; });
    held.hold(QStringLiteral("sk-ant-one"));
    held.forget();
    check(held.key().isEmpty() && !held.expiresAt().isValid() && changes == 2, "forget drops the key");
    held.forget();
    check(changes == 2, "forgetting nothing announces nothing");
    held.hold(QStringLiteral("sk-ant-two"));
    check(!held.hold(QStringLiteral("   ")).isValid() && held.key().isEmpty(), "an empty key forgets");
  }

  std::printf("the expiry timer:\n");
  {
    SessionKey held(80);
    bool dropped = false;
    QObject::connect(&held, &SessionKey::changed, [&held, &dropped] {
      dropped = !held.expiresAt().isValid();
    });
    held.hold(QStringLiteral("sk-ant-short"));
    QElapsedTimer waited;
    waited.start();
    while (!dropped && waited.elapsed() < 3000) QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents, 50);
    check(dropped, "the timer drops the key at the TTL with nothing reading it");
    check(waited.elapsed() >= 70, "not before the TTL");
  }

  std::printf("the process-wide holder:\n");
  {
    SessionKey& held = SessionKey::instance();
    check(&held == &SessionKey::instance() && held.parent() == &app, "one holder, owned by the app");
    held.hold(QStringLiteral("sk-ant-process"));
    QTimer::singleShot(0, &app, &QCoreApplication::quit);
    app.exec();
    check(held.key().isEmpty(), "quitting the application wipes the key");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
