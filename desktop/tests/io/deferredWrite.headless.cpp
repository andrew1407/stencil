// Headless check of io/deferredWrite: a burst coalesces into one write, an older hand-off that
// reaches the write gate after a newer one never lands over it, and an owner-only file is 0600.
// QtCore only, a scratch dir, no display.
#include "deferredWrite.hpp"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QFile>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThreadPool>
#include <atomic>
#include <memory>

#include "../support/check.hpp"

namespace dw = stencil::gui::deferredWrite;

namespace {
  QByteArray readAll(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
  }

  // Turns the loop until `cond` holds or 3 s pass.
  template <class F>
  bool pumpUntil(F cond) {
    QDeadlineTimer deadline(3000);
    while (!cond() && !deadline.hasExpired()) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return cond();
  }
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir dir;
  check(dir.isValid(), "a scratch dir");

  // A burst of schedules inside the window is one build and one write: the last one's bytes.
  const QString burst = dir.filePath(QStringLiteral("burst.json"));
  auto builds = std::make_shared<std::atomic<int>>(0);
  for (const char* text : {"one", "two", "three"})
    dw::schedule(burst, 20, [builds, text] { ++*builds; return QByteArray(text); });
  dw::flush();
  check(*builds == 1 && readAll(burst) == "three", "a burst coalesces into one write of the last bytes");

  // The first hand-off is held inside its build, off the write gate, while a newer one lands.
  const QString raced = dir.filePath(QStringLiteral("raced.json"));
  auto held = std::make_shared<QSemaphore>(0);
  auto started = std::make_shared<std::atomic<bool>>(false);
  dw::schedule(raced, 0, [held, started] {
    *started = true;
    held->acquire();
    return QByteArray("older");
  });
  check(pumpUntil([started] { return started->load(); }), "the older write is on the pool, building");
  dw::schedule(raced, 0, [] { return QByteArray("newer"); });
  check(pumpUntil([&raced] { return readAll(raced) == "newer"; }), "the newer write lands first");
  held->release();
  dw::flush();
  check(readAll(raced) == "newer", "an older hand-off finishing last never lands over the newer bytes");

  // A file that holds a secret is the owner's alone.
  const QString secret = dir.filePath(QStringLiteral("secret.json"));
  check(dw::atomic(secret, "{}", /*ownerOnly=*/true), "an owner-only write succeeds");
  check(QFile::permissions(secret) == (QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                       QFileDevice::ReadUser | QFileDevice::WriteUser),
        "an owner-only file is 0600");

  QThreadPool::globalInstance()->waitForDone();
  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}
