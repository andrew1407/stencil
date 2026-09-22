// Headless check of io/MediaLoader's candidate list (loadFirstOf) — the desktop twin of
// fetchFirstDraggedMediaFile in browser/js/core/pointer/dragImageUrl.js: it advances past a
// candidate that cannot resolve, stops at the first that can, and reports the FIRST failure
// when none does. The failing candidates are guard-blocked hosts, so nothing is fetched.
#include "MediaLoader.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <functional>

#include "../support/check.hpp"

using stencil::gui::MediaLoader;

// Blocked in loose mode too (private / link-local / TEST-NET), so resolve() refuses them
// without a socket.
static const QString BLOCKED_A = QStringLiteral("http://10.0.0.1/first.png");
static const QString BLOCKED_B = QStringLiteral("http://169.254.169.254/second.png");
static const QString BLOCKED_C = QStringLiteral("http://192.0.2.7/third");

namespace {

  struct Run {
    int loads = 0;
    int fails = 0;
    QString error;
    QString localPath;
  };

  // Drives one load and pumps the event loop until it answers (or the bound elapses).
  Run drive(MediaLoader& loader, const std::function<void()>& start) {
    Run run;
    QEventLoop loop;
    QObject::connect(&loader, &MediaLoader::loaded, &loop,
                     [&](const QImage&, const QString& path) {
                       ++run.loads;
                       run.localPath = path;
                       loop.quit();
                     });
    QObject::connect(&loader, &MediaLoader::failed, &loop, [&](const QString& message) {
      ++run.fails;
      run.error = message;
      loop.quit();
    });
    QTimer::singleShot(0, &loop, start);
    QTimer::singleShot(4000, &loop, &QEventLoop::quit);
    loop.exec();
    loader.disconnect(&loop);
    return run;
  }

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  QTemporaryDir dir;
  const QString png = QDir(dir.path()).filePath(QStringLiteral("real.png"));
  QImage made(8, 6, QImage::Format_ARGB32);
  made.fill(Qt::blue);
  check(made.save(png), "the fixture image is on disk");

  MediaLoader loader;

  Run run = drive(loader, [&] { loader.loadFirstOf({BLOCKED_A, png}, 0); });
  check(run.loads == 1 && run.fails == 0, "a refused first candidate falls through to the next");
  check(run.localPath == png, "the candidate that answered is the one that loads");

  run = drive(loader, [&] { loader.loadFirstOf({BLOCKED_A, BLOCKED_B, png}, 0); });
  check(run.loads == 1 && run.fails == 0, "the list is walked until one of them resolves");

  run = drive(loader, [&] { loader.loadFirstOf({BLOCKED_A, BLOCKED_B, BLOCKED_C}, 0); });
  check(run.fails == 1 && run.loads == 0, "every candidate failing reports exactly one failure");
  check(run.error.contains(QStringLiteral("10.0.0.1")),
        "the report is the FIRST failure — the preferred candidate explains the drop");
  check(!run.error.contains(QStringLiteral("192.0.2.7")), "the last failure is not the one shown");

  run = drive(loader, [&] { loader.loadFirstOf({png, BLOCKED_A}, 0); });
  check(run.loads == 1 && run.fails == 0, "a first candidate that resolves ends the walk");

  run = drive(loader, [&] { loader.loadFirstOf({QString(), QStringLiteral("  "), png}, 0); });
  check(run.loads == 1, "blank candidates are dropped, not attempted");

  // A plain load() keeps its single-source contract: no leftover list to fall through into.
  run = drive(loader, [&] { loader.load(BLOCKED_B, 0); });
  check(run.fails == 1 && run.error.contains(QStringLiteral("169.254.169.254")),
        "a plain load() after a candidate walk reports its own source and stops");

  run = drive(loader, [&] { loader.loadFirstOf({BLOCKED_C}, 0); });
  check(run.fails == 1 && run.error.contains(QStringLiteral("192.0.2.7")),
        "a one-entry list is a plain load");

  run = drive(loader, [&] { loader.loadFirstOf({}, 0); });
  check(run.fails == 1 && run.loads == 0, "an empty list still answers once");

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}
