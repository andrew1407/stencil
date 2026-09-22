#include "dropSources.hpp"

#include <QImage>

#include <QByteArray>
#include <QDebug>
#include <QMimeData>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QUrl>

namespace stencil::gui {

  namespace {
    // '//host' is fixable (https); a root-relative '/path' has an unknowable origin and is refused.
    QString absolutize(const QString& raw) {
      const QString u = raw.trimmed();
      static const QStringList KEPT{QStringLiteral("http://"), QStringLiteral("https://"),
                                    QStringLiteral("data:"), QStringLiteral("blob:")};
      for (const QString& p : KEPT)
        if (u.startsWith(p, Qt::CaseInsensitive)) return u;
      if (u.startsWith(QLatin1String("//"))) return QStringLiteral("https:") + u;
      return {};
    }

    // A drag fragment is SERIALISED html, so its src arrives escaped: "?s=64&amp;v=4" names nothing.
    QString unescapeEntities(QString s) {
      s.replace(QLatin1String("&lt;"), QLatin1String("<"));
      s.replace(QLatin1String("&gt;"), QLatin1String(">"));
      s.replace(QLatin1String("&quot;"), QLatin1String("\""));
      s.replace(QLatin1String("&#39;"), QLatin1String("'"));
      s.replace(QLatin1String("&amp;"), QLatin1String("&"));   // last: "&amp;lt;" is the text "&lt;"
      return s;
    }

    // A drag payload may arrive UTF-16 with a BOM, so the BOM decides; UTF-8 is the fallback.
    QString decodeBytes(const QByteArray& raw) {
      if (raw.isEmpty()) return {};
      const auto enc = QStringConverter::encodingForData(raw);
      return QStringDecoder(enc.value_or(QStringConverter::Utf8)).decode(raw);
    }

    QString decodedText(const QMimeData* mime, QLatin1String format) {
      return decodeBytes(mime->data(format));
    }

    // Twin of looksLikeImageUrl (browser/js/core/pointer/dragImageUrl.js): only a tiebreak below.
    bool namesAnImage(const QString& u) {
      static const QRegularExpression rx(
          QStringLiteral("\\.(png|jpe?g|gif|webp|avif|bmp|ico|tiff?|svg)([?#]|$)"),
          QRegularExpression::CaseInsensitiveOption);
      return rx.match(u).hasMatch() || u.startsWith(QLatin1String("data:image/"), Qt::CaseInsensitive)
          || u.startsWith(QLatin1String("blob:"), Qt::CaseInsensitive);
    }

    bool isHttpUrl(const QString& u) {
      return u.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
          || u.startsWith(QLatin1String("https://"), Qt::CaseInsensitive);
    }

    QString imgSrcFromHtml(const QString& html) {
      static const QRegularExpression rx(QStringLiteral("<img[^>]+src\\s*=\\s*[\"']([^\"']+)[\"']"),
                                         QRegularExpression::CaseInsensitiveOption);
      return absolutize(unescapeEntities(rx.match(html).captured(1)));
    }
  }  // namespace

  // extractDraggedImageUrls: whatever NAMES an image first, in source order (a gallery lists the
  // full size and renders the thumbnail); then the promised file, the <img> src and the bitmap.
  QStringList rankedImageUrls(const QMimeData* mime, const QString& bitmap,
                              const support::DragPasteboard& native) {
    const QUrl listed = mime->urls().value(0);
    const QString fromList = listed.isEmpty() ? QString() : absolutize(listed.toString());
    QString fromHtml = imgSrcFromHtml(decodedText(mime, QLatin1String("text/html")));
    if (fromHtml.isEmpty()) fromHtml = imgSrcFromHtml(decodeBytes(native.html));
    // "URL\ntitle", and some sources fill only it (extension twin: lib/drop/dragUrl.js).
    const QString fromMoz = absolutize(
        decodedText(mime, QLatin1String("text/x-moz-url")).section(QLatin1Char('\n'), 0, 0));
    QString fromText = mime->hasText() ? mime->text().trimmed() : QString();
    if (!fromText.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
        && !fromText.startsWith(QLatin1String("https://"), Qt::CaseInsensitive))
      fromText.clear();

    QStringList inOrder, ranked;
    for (const QString& c : {fromList, fromHtml, fromMoz, fromText})
      if (!c.isEmpty()) inOrder << c;
    for (const QString& u : native.urls) {
      const QString a = absolutize(u);
      if (!a.isEmpty() && !inOrder.contains(a)) inOrder << a;
    }
    const auto add = [&ranked](const QString& u) {
      if (!u.isEmpty() && !ranked.contains(u)) ranked << u;
    };
    for (const QString& c : inOrder)
      if (namesAnImage(c)) add(c);
    // Bytes the drag source already wrote: nothing left to fetch, so it catches the named url.
    add(native.filePath);
    add(fromHtml);
    add(bitmap);
    for (const QString& c : inOrder) add(c);
    return ranked;
  }

  DropSrc droppableSource(const QMimeData* mime, const QString& bitmap,
                          const support::DragPasteboard& native) {
    if (!mime) return {};
    for (const QUrl& u : mime->urls())
      if (u.isLocalFile()) return { DropSrc::LOCAL_FILE, u.toLocalFile(), {} };
    const QStringList urls = rankedImageUrls(mime, bitmap, native);
    if (!urls.isEmpty()) return { DropSrc::URL, urls.first(), urls.mid(1) };
    return {};
  }

  bool isLinkOnlyDrag(const QMimeData* mime, const QString& bitmap,
                      const support::DragPasteboard& native) {
    if (!mime || !bitmap.isEmpty() || !native.filePath.isEmpty()) return false;
    for (const QUrl& u : mime->urls())
      if (u.isLocalFile()) return false;
    // An <img> src is a picture whatever its url spells, so its presence is not a link-only drag.
    if (!imgSrcFromHtml(decodedText(mime, QLatin1String("text/html"))).isEmpty()) return false;
    if (!imgSrcFromHtml(decodeBytes(native.html)).isEmpty()) return false;
    const QStringList ranked = rankedImageUrls(mime, bitmap, native);
    if (ranked.isEmpty()) return false;
    for (const QString& u : ranked)
      if (namesAnImage(u) || !isHttpUrl(u)) return false;
    return true;
  }

  bool canDrop(const QMimeData* mime) {
    if (!mime) return false;
    if (mime->hasImage()) return true;
    return droppableSource(mime).kind != DropSrc::NONE;
  }

  void logDroppedMime(const QMimeData* mime, const support::DragPasteboard& native) {
    if (!mime || qEnvironmentVariableIntValue("STENCIL_DEBUG_DROP") == 0) return;
    const QImage bmp = qvariant_cast<QImage>(mime->imageData());
    qWarning() << "stencil drop: formats" << mime->formats() << "hasHtml" << mime->hasHtml()
               << "hasImage" << mime->hasImage() << "bitmap" << bmp.size()
               << "urls" << mime->urls()
               << "html" << decodedText(mime, QLatin1String("text/html")).left(400)
               << "native html" << decodeBytes(native.html).left(400)
               << "native urls" << native.urls << "promised file" << native.filePath
               << "ranked" << rankedImageUrls(mime, QString(), native);
  }

}  // namespace stencil::gui
