// io/MediaLoader's remote branches behind one guard verdict, DNS included: a link-delivered source
// (setStrictFetch) whose NAME resolves to a refused address fails before any media player exists,
// whether it looks like a video or an image, so Qt's FFmpeg backend never dials it.
#include "MediaLoader.hpp"

#include <QBuffer>
#include <QImage>
#include <QMediaPlayer>
#include <QString>

#include "../support/check.hpp"
#include "mediaLoaderDrive.hpp"

using stencil::gui::MediaLoader;
using stencil::test::drive;
using stencil::test::Run;

void checkGuardedRemote() {
  // "localhost." is a name to the literal check, and resolves to loopback, which strict refuses.
  for (const QString& src : {QStringLiteral("http://localhost.:9/clip.mp4"), QStringLiteral("http://localhost.:9/picture")}) {
    MediaLoader loader;
    loader.setStrictFetch(true);
    const Run run = drive(loader, [&] { loader.load(src, 0); });
    check(run.fails == 1 && run.loads == 0, "a strict source whose name resolves to loopback fails once");
    check(run.error.contains(QStringLiteral("refusing to fetch")), "…refused by the guard, not by a timeout");
    check(loader.findChildren<QMediaPlayer*>().isEmpty(), "…before any media player was built");
  }
}

// The shared decode cap (io/mediaTypes.cpp decodeCapped): a header past LIMITS.imageMaxSide is refused
// before a pixel is allocated, and a picture past Qt's default 256 MB allocation limit, yet within
// the cap, decodes as it does on every other surface.
void checkDecodeCap() {
  // A real 20000 x 1 PNG: tiny on disk, one side past the cap.
  QByteArray png;
  {
    QImage wide(20000, 1, QImage::Format_RGB32);
    wide.fill(Qt::red);
    QBuffer out(&png);
    out.open(QIODevice::WriteOnly);
    wide.save(&out, "PNG");
  }
  bool tooLarge = false;
  check(stencil::gui::decodeCapped(png, &tooLarge).isNull() && tooLarge, "a header past the side cap is refused as too large");
  check(stencil::gui::imageTooLargeMessage().contains(QStringLiteral("16384")), "…with the shared message");

  QImage big(8200, 8200, QImage::Format_RGB32);   // 269 MB decoded: past Qt's own 256 MB default
  big.fill(Qt::darkGreen);
  QByteArray bytes;
  QBuffer buf(&bytes);
  buf.open(QIODevice::WriteOnly);
  big.save(&buf, "PNG", 100);
  big = QImage();
  const QImage back = stencil::gui::decodeCapped(bytes, &tooLarge);
  check(!tooLarge && back.size() == QSize(8200, 8200), "a 67 MP picture within the cap decodes");
}
