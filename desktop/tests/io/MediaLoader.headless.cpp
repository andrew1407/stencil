// Headless check of io/MediaLoader's candidate list (loadFirstOf) — the desktop twin of
// fetchFirstDraggedMediaFile in browser/js/core/pointer/dragImageUrl.js: it advances past a
// candidate that cannot resolve, stops at the first that can, and reports the FIRST failure
// when none does (guard-blocked hosts, so nothing is fetched); every decode is off-thread. Its
// sibling TUs walk the decode routing and the shared image-header corpus.
#include "MediaLoader.hpp"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>

#include "../support/check.hpp"
#include "mediaLoaderDrive.hpp"

using stencil::gui::MediaLoader;
using stencil::test::drive;
using stencil::test::Run;

void checkImageHeaderCorpus();                  // imageHeaderFixtures.headless.cpp
void checkDecodeRouting(const QString& dir);    // mediaLoaderRouting.headless.cpp

// Blocked in loose mode too (private / link-local / TEST-NET), so resolve() refuses them
// without a socket.
static const QString BLOCKED_A = QStringLiteral("http://10.0.0.1/first.png");
static const QString BLOCKED_B = QStringLiteral("http://169.254.169.254/second.png");
static const QString BLOCKED_C = QStringLiteral("http://192.0.2.7/third");

namespace {

  // Large enough that its decode spans many turns of a 1 ms timer.
  QImage bigPicture() {
    QImage img(4000, 3000, QImage::Format_RGB32);
    for (int y = 0; y < img.height(); ++y) {
      auto* row = reinterpret_cast<QRgb*>(img.scanLine(y));
      for (int x = 0; x < img.width(); ++x) row[x] = qRgb(x & 255, y & 255, (x ^ y) & 255);
    }
    return img;
  }

  QString pngBase64(const QImage& img) {
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return QString::fromLatin1(bytes.toBase64());
  }

}  // namespace

int main(int argc, char** argv) {
  // AVFoundation answers on the main CFRunLoop, which a Darwin QCoreApplication turns only on request.
  qputenv("QT_EVENT_DISPATCHER_CORE_FOUNDATION", "1");
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

  // Every decode runs on the pool: load() returns first, and the GUI loop keeps turning meanwhile.
  const QString big = QDir(dir.path()).filePath(QStringLiteral("big.png"));
  check(bigPicture().save(big, "PNG"), "the large fixture is on disk");
  for (const QString& src : {big, QString::fromLatin1("data:image/png;base64,") + pngBase64(made)}) {
    int ticks = 0;
    QTimer beat;
    beat.setInterval(1);
    QObject::connect(&beat, &QTimer::timeout, [&ticks] { ++ticks; });
    bool loading = true;
    bool answeredInside = false;
    const QMetaObject::Connection inside = QObject::connect(&loader, &MediaLoader::loaded, [&] {
      answeredInside = answeredInside || loading;
    });
    run = drive(loader, [&] {
      beat.start();
      loader.load(src, 0);
      loading = false;
    });
    QObject::disconnect(inside);
    beat.stop();
    check(run.loads == 1 && !answeredInside, "a picture lands from the event loop, never inside load()");
    if (src == big) check(ticks >= 5, "the GUI loop kept turning while the large picture decoded");
  }

  // A load that a newer one overtakes is dropped: only the newer picture answers.
  run = drive(loader, [&] {
    loader.load(big, 0);
    loader.load(png, 0);
  });
  QThreadPool::globalInstance()->waitForDone();   // the overtaken decode has finished too…
  int late = 0;
  QObject::connect(&loader, &MediaLoader::loaded, [&late] { ++late; });
  QCoreApplication::processEvents();              // …and its answer had its chance to land
  check(run.loads == 1 && run.localPath == png && late == 0, "the overtaken picture never answers");

  // A loader destroyed mid-decode answers nothing, and nothing lands on it.
  int answers = 0;
  auto* doomed = new MediaLoader;
  QObject::connect(doomed, &MediaLoader::loaded, [&answers] { ++answers; });
  doomed->load(big, 0);
  delete doomed;
  QThreadPool::globalInstance()->waitForDone();
  QCoreApplication::processEvents();
  check(answers == 0, "a decode outliving its loader is discarded");

  checkDecodeRouting(dir.path());
  checkImageHeaderCorpus();

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}
