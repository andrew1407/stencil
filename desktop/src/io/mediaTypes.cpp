// What kind of media a source is: by suffix off mediaTypes.json `surfaces.desktop`, and by the
// leading bytes through the codec-free header sniffer the shared corpus
// common/fixtures/imageHeader pins (twin of cli/src/scrape/sniff.zig).
#include "MediaLoader.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <limits>

// app.qrc's shared-config canon, forced live on first read — a pre-main caller can
// beat the resource's own initializer. Global scope, for Q_INIT_RESOURCE.
static void ensureAppResources() { Q_INIT_RESOURCE(app); }

namespace stencil::gui {

  namespace {
    // common/config/mediaTypes.json `surfaces.desktop`. Deliberately not the wider
    // `video.extensions` contract set - that would accept more files (the asset's `drift` note).
    QStringList canonExtensions(const QString& kind) {
      ensureAppResources();
      QFile f(QStringLiteral(":/config/mediaTypes.json"));
      if (!f.open(QIODevice::ReadOnly)) return {};
      const QJsonObject desktop = QJsonDocument::fromJson(f.readAll())
                                      .object()
                                      .value("surfaces")
                                      .toObject()
                                      .value("desktop")
                                      .toObject();
      QStringList exts;
      for (const QJsonValue& v : desktop.value(kind).toArray()) exts << v.toString();
      return exts;
    }

    quint32 byteAt(QByteArrayView b, qsizetype o) { return quint8(b[o]); }
    quint32 u16be(QByteArrayView b, qsizetype o) { return byteAt(b, o) << 8 | byteAt(b, o + 1); }
    quint32 u16le(QByteArrayView b, qsizetype o) { return byteAt(b, o + 1) << 8 | byteAt(b, o); }
    quint32 u24le(QByteArrayView b, qsizetype o) { return byteAt(b, o + 2) << 16 | u16le(b, o); }
    quint32 u32be(QByteArrayView b, qsizetype o) { return u16be(b, o) << 16 | u16be(b, o + 2); }
    quint32 u32le(QByteArrayView b, qsizetype o) { return u16le(b, o + 2) << 16 | u16le(b, o); }
    // A BMP side is signed (negative = top-down); its size is the magnitude.
    quint32 magnitude(quint32 raw) {
      const qint64 v = qint32(raw);
      return quint32(v < 0 ? -v : v);
    }

    QString signatureOf(QByteArrayView b) {
      if (b.startsWith("\x89PNG\r\n\x1a\n")) return QStringLiteral("png");
      if (b.startsWith("GIF87a") || b.startsWith("GIF89a")) return QStringLiteral("gif");
      if (b.startsWith("BM")) return QStringLiteral("bmp");
      if (b.startsWith("\xFF\xD8")) return QStringLiteral("jpeg");
      if (b.startsWith("RIFF") && b.size() >= 12 && b.sliced(8, 4) == "WEBP") return QStringLiteral("webp");
      return {};
    }

    // Walks the segments to the first frame header: SOF0–SOF15 except DHT C4, JPG C8 and DAC CC.
    void measureJpeg(QByteArrayView b, ImageHeader& h) {
      qsizetype pos = 2;
      while (pos + 9 <= b.size()) {
        const quint32 marker = byteAt(b, pos + 1);
        if (byteAt(b, pos) != 0xFF || marker == 0xFF) {
          ++pos;
          continue;
        }
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD9)) {
          pos += 2;
          continue;
        }
        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
          h.height = u16be(b, pos + 5);
          h.width = u16be(b, pos + 7);
          return;
        }
        const quint32 segment = u16be(b, pos + 2);
        if (segment < 2) return;
        pos += 2 + segment;
      }
    }

    void measureWebp(QByteArrayView b, ImageHeader& h) {
      const QByteArrayView chunk = b.sliced(12, 4);
      if (chunk == "VP8 " && b.sliced(23, 3) == "\x9D\x01\x2A") {
        h.width = u16le(b, 26) & 0x3FFF;   // the top two bits are the scale
        h.height = u16le(b, 28) & 0x3FFF;
      } else if (chunk == "VP8L" && byteAt(b, 20) == 0x2F) {
        h.width = 1 + ((byteAt(b, 22) & 0x3F) << 8 | byteAt(b, 21));
        h.height = 1 + ((byteAt(b, 24) & 0x0F) << 10 | byteAt(b, 23) << 2 | (byteAt(b, 22) & 0xC0) >> 6);
      } else if (chunk == "VP8X") {
        h.width = 1 + u24le(b, 24);
        h.height = 1 + u24le(b, 27);
      }
    }
  }  // namespace

  bool isVideoFileName(const QString& path) {
    static const QStringList VIDEO_EXT = canonExtensions(QStringLiteral("video"));
    return VIDEO_EXT.contains(QFileInfo(path).suffix().toLower());
  }

  bool isImageFileName(const QString& path) {
    static const QStringList IMAGE_EXT = canonExtensions(QStringLiteral("image"));
    return IMAGE_EXT.contains(QFileInfo(path).suffix().toLower());
  }

  ImageHeader sniffImageHeader(QByteArrayView head) {
    ImageHeader h;
    h.format = signatureOf(head);
    const qsizetype n = head.size();
    if (h.format == QLatin1String("png") && n >= 24 && head.sliced(12, 4) == "IHDR") {
      h.width = u32be(head, 16);
      h.height = u32be(head, 20);
      constexpr quint32 PNG_MAX_SIDE = std::numeric_limits<qint32>::max();   // the PNG spec's cap
      if (h.width > PNG_MAX_SIDE || h.height > PNG_MAX_SIDE) h.width = h.height = 0;
    } else if (h.format == QLatin1String("gif") && n >= 10) {
      h.width = u16le(head, 6);
      h.height = u16le(head, 8);
    } else if (h.format == QLatin1String("bmp") && n >= 26) {
      h.width = magnitude(u32le(head, 18));
      h.height = magnitude(u32le(head, 22));
    } else if (h.format == QLatin1String("jpeg") && n >= 4) {
      measureJpeg(head, h);
    } else if (h.format == QLatin1String("webp") && n >= 30) {
      measureWebp(head, h);
    }
    if (h.width == 0 || h.height == 0) h.width = h.height = 0;   // a zero side is no answer
    return h;
  }

}
