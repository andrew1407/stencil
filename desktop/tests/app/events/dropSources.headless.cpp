// What the NATIVE drag pasteboard adds to the ranked candidates (app/events/dropSources.cpp):
// the html flavor Qt never maps, the urls it dropped, and a promised file. AppKit is not
// reachable from a synthesised drag, so the finds are injected through the DragPasteboard seam
// and the promise's bounded wait is driven by a poll of the test's own.
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMimeData>
#include <QUrl>
#include <cstdio>

#include "dragPasteboard.hpp"
#include "dropSources.hpp"
#include "support/check.hpp"

using stencil::gui::isLinkOnlyDrag;
using stencil::gui::rankedImageUrls;
using stencil::support::DragPasteboard;

namespace {
  const QString AVATAR = QStringLiteral("https://avatars.githubusercontent.com/u/43030001?v=4");
  const QString LINK = QStringLiteral("https://github.com/account");
  const QString FULL_SIZE = QStringLiteral("https://example.com/full.jpg");
  const QString BITMAP = QStringLiteral("data:image/png;base64,iVBORw0KGgo=");
  const QString PROMISED = QStringLiteral("/tmp/stencil-drop/dragged.png");

  QString wrappedImg(const QString& src) {
    return QStringLiteral("<a href=\"/account\"><img class=\"avatar\" src=\"%1\" /></a>").arg(src);
  }

  void linkOnlyDrag(QMimeData& mime) {
    mime.setUrls({QUrl(LINK)});
    mime.setText(LINK);
  }

  QString joined(const QStringList& l) { return l.join(QLatin1Char(' ')); }

  void checkList(const QStringList& got, const QStringList& want, const char* msg) {
    check(got == want, msg);
    if (got != want)
      std::printf("       got  [%s]\n       want [%s]\n", qPrintable(joined(got)),
                  qPrintable(joined(want)));
  }
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  // The reported failure: Chrome crossed the pasteboard, Qt showed only the wrapper link.
  {
    QMimeData mime;
    linkOnlyDrag(mime);
    DragPasteboard native;
    native.html = wrappedImg(AVATAR).toUtf8();
    checkList(rankedImageUrls(&mime, QString(), native), {AVATAR, LINK},
              "the native html flavor recovers the <img> the link wrapped");
  }

  // The gate in front of the page scan: only a drag whose every candidate is a page url.
  {
    QMimeData bare;
    linkOnlyDrag(bare);
    check(isLinkOnlyDrag(&bare), "a wrapper link alone is a link-only drag");
    DragPasteboard native;
    native.html = wrappedImg(AVATAR).toUtf8();
    check(!isLinkOnlyDrag(&bare, QString(), native), "an <img> src on the pasteboard is not");
    native = {};
    native.filePath = PROMISED;
    check(!isLinkOnlyDrag(&bare, QString(), native), "nor is a promised file");
    check(!isLinkOnlyDrag(&bare, BITMAP), "nor is a rendered bitmap");
    QMimeData named;
    named.setUrls({QUrl(FULL_SIZE)});
    check(!isLinkOnlyDrag(&named), "nor is a url that names an image");
    QMimeData local;
    local.setUrls({QUrl::fromLocalFile(QStringLiteral("/tmp/a.png"))});
    check(!isLinkOnlyDrag(&local), "nor is a local file");
  }

  // A promised file is bytes in hand, so it backs up the url that names an image, never replaces it.
  {
    QMimeData mime;
    mime.setUrls({QUrl(FULL_SIZE)});
    DragPasteboard native;
    native.filePath = PROMISED;
    checkList(rankedImageUrls(&mime, BITMAP, native), {FULL_SIZE, PROMISED, BITMAP},
              "a named url keeps the head and the promised file catches it");
  }

  // Nothing here names an image: the file the browser wrote beats both the link and the bitmap.
  {
    QMimeData mime;
    linkOnlyDrag(mime);
    DragPasteboard native;
    native.filePath = PROMISED;
    checkList(rankedImageUrls(&mime, BITMAP, native), {PROMISED, BITMAP, LINK},
              "a promised file outranks the rendered bitmap and the wrapper link");
  }

  // A url flavor Qt left on the pasteboard is a candidate like any other.
  {
    QMimeData mime;
    linkOnlyDrag(mime);
    DragPasteboard native;
    native.urls = {QStringLiteral("ftp://example.com/x.png"), FULL_SIZE};
    checkList(rankedImageUrls(&mime, QString(), native), {FULL_SIZE, LINK},
              "a native url joins the walk, and a scheme we cannot fetch does not");
  }

  // Off Apple, and on any drag the reader finds nothing in, the ranking is the one shipped before.
  {
    QMimeData mime;
    mime.setUrls({QUrl(LINK)});
    mime.setHtml(wrappedImg(AVATAR));
    mime.setText(LINK);
    checkList(rankedImageUrls(&mime, BITMAP, DragPasteboard{}),
              rankedImageUrls(&mime, BITMAP), "an empty native find ranks identically");
    checkList(rankedImageUrls(&mime, BITMAP), {AVATAR, BITMAP, LINK},
              "…and that ranking is still the shipped one");
  }

  {
    QElapsedTimer clock;
    clock.start();
    const QString gaveUp = stencil::support::awaitFile([] { return QString(); }, 120);
    const qint64 spent = clock.elapsed();
    check(gaveUp.isEmpty() && spent >= 120, "a promise that never lands gives up on its deadline");
    check(spent < 1000, "…and gives up AT the deadline, so the walk carries on");

    int calls = 0;
    const QString landed = stencil::support::awaitFile(
        [&calls] { return ++calls < 3 ? QString() : PROMISED; }, 5000);
    check(landed == PROMISED, "a promise that lands is taken as soon as it does");
  }

  {
    const QString dir = stencil::support::dropScratchDir();
    check(!dir.isEmpty() && QDir(dir).exists(), "the promise scratch dir is created");
    const QFile::Permissions perms = QFile::permissions(dir);
    check(!(perms & (QFileDevice::ReadGroup | QFileDevice::ReadOther)),
          "…owner-only, so a promised file is never world-readable");
    QFile stale(QDir(dir).filePath(QStringLiteral("stale.png")));
    check(stale.open(QIODevice::WriteOnly), "a leftover file can be planted");
    stale.close();
    check(stencil::support::dropScratchDir() == dir && !stale.exists(),
          "…and the next read takes the previous drop's file away");
  }

#ifndef Q_OS_MACOS
  {
    const stencil::support::pasteboard::Offer offer = stencil::support::pasteboard::read();
    check(offer.html.isEmpty() && offer.urls.isEmpty() && !offer.promise,
          "the non-Apple stub offers nothing");
    check(!stencil::support::pasteboard::fetch(QStringLiteral("/tmp"))
              && stencil::support::pasteboard::poll().isEmpty(),
          "…and fetches nothing, so readDragPasteboard() is inert there");
  }
#endif

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
