// io/MediaLoader's route for a picture that will not decode: bytes whose header names a still image
// fail at once with the loader's own words — a local file, an inline payload and a download alike —
// while bytes with no such signature still reach the video decoder, and a real clip still opens
// through it. The download comes from a loopback stand-in, so nothing leaves the machine.
#include "MediaLoader.hpp"

#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <cstdio>

#include "../support/check.hpp"
#include "../support/heldImageServer.hpp"
#include "mediaLoaderDrive.hpp"
#include "quickTimeClip.hpp"

using stencil::gui::MediaLoader;
using stencil::test::drive;
using stencil::test::Run;

namespace {

  constexpr qint64 FAST_MS = 1000;   // the video probe it skips waits 20 s

  QByteArray encoded(const char* format) {
    QImage img(64, 48, QImage::Format_RGB32);
    for (int y = 0; y < img.height(); ++y)
      for (int x = 0; x < img.width(); ++x) img.setPixel(x, y, qRgb(x * 4, y * 5, (x ^ y) * 3));
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, format);
    return bytes;
  }

  // A frame header claiming 17-bit samples, which no JPEG decoder takes.
  QByteArray corruptJpeg() {
    QByteArray jpg = encoded("JPG");
    const qsizetype sof = jpg.indexOf("\xFF\xC0");
    if (sof > 0) jpg[sof + 4] = 17;
    return jpg;
  }

  QString written(const QString& dir, const char* name, const QByteArray& bytes) {
    const QString path = QDir(dir).filePath(QString::fromLatin1(name));
    QFile f(path);
    check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), name);
    return path;
  }

  void failsFast(MediaLoader& loader, const QString& src, const QString& want, const char* what) {
    QElapsedTimer clock;
    clock.start();
    const Run run = drive(loader, [&] { loader.load(src, 0); });
    const qint64 ms = clock.elapsed();
    check(run.fails == 1 && run.loads == 0 && run.error == want, what);
    check(ms < FAST_MS && !loader.isVideoSource(), "  …at once, never handed to the video decoder");
    if (ms >= FAST_MS || run.error != want)
      std::printf("       %lld ms: \"%s\"\n", ms, qPrintable(run.error));
  }

  QString unreadable(const QString& src) { return QStringLiteral("Not a readable image or video: ") + src; }

}  // namespace

void checkDecodeRouting(const QString& dir) {
  std::printf("decode routing:\n");
  MediaLoader loader;
  const QByteArray png = encoded("PNG");
  const QByteArray halfPng = png.left(png.size() / 2);

  const QString truncated = written(dir, "truncated.png", halfPng);
  failsFast(loader, truncated, unreadable(truncated), "a truncated PNG fails");
  const QString corrupt = written(dir, "corrupt.jpg", corruptJpeg());
  failsFast(loader, corrupt, unreadable(corrupt), "a corrupt JPEG fails");
  const QString bare = written(dir, "picture", png.left(20));
  failsFast(loader, bare, unreadable(bare), "an extensionless file with a PNG signature fails");
  failsFast(loader, QStringLiteral("data:image/png;base64,") + QString::fromLatin1(halfPng.toBase64()),
            QStringLiteral("Could not decode the inline image"), "a truncated inline PNG fails");

  stencil::test::HeldImageServer http;
  check(http.listen(), "the loopback server listens");
  http.png = halfPng;
  http.hold = false;
  failsFast(loader, http.url(), unreadable(http.url()), "a truncated PNG download fails");

  // No still-image signature: the video decoder still gets its chance, as before.
  const QString junk = written(dir, "notes.bin", QByteArray(64, 'x'));
  drive(loader, [&] { loader.load(junk, 0); });
  check(loader.isVideoSource(), "bytes with no still-image signature still reach the video decoder");

  const QByteArray clip = stencil::test::quickTimeClip(QSize(64, 48), 10);
  const QString bareClip = written(dir, "clip", clip);
  drive(loader, [&] { loader.load(bareClip, 0); });
  check(loader.isVideoSource(), "a clip with no suffix is handed to the video decoder");
  const QString mov = written(dir, "clip.mov", clip);
  const Run run = drive(loader, [&] { loader.load(mov, 0); });
  if (run.loads == 1) check(loader.isVideoSource(), "a real clip opens through the video path");
  else std::printf("  (skip) this media backend plays no QuickTime clip: %s\n", qPrintable(run.error));
}
