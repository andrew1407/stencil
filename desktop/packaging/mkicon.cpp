// Rasterise an SVG into a platform icon container: a Windows .ico, a macOS .icns, or a plain
// .png at one size. Both containers are typed headers around PNG frames, so this one tool
// replaces sips, iconutil and the shell scripts that drove them.
//
// Usage: stencil_mkicon <input.svg> <output.ico|.icns|.png> [size]

#include <QBuffer>
#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMap>
#include <QPainter>
#include <QStringList>
#include <QSvgRenderer>

#include <cstdio>

namespace {

// 96 DPI in the PNG's pHYs chunk, in dots per metre: no icon loader reads it, but pinning it
// keeps the output byte-reproducible instead of following whatever the build machine reports.
constexpr int DOTS_PER_METRE = 3780;

// 16 is the Windows title bar, 256 the Explorer "extra large" view; the rest it scales between.
constexpr int ICO_SIZES[] = {16, 20, 24, 32, 40, 48, 64, 128, 256};

// The OSType of each .icns slot and the square it holds. A retina slot repeats the size of the
// 1x slot above it, which is how iconutil lays out a full .iconset.
struct IcnsSlot {
  const char* type;
  int px;
};
constexpr IcnsSlot ICNS_SLOTS[] = {
  {"icp4", 16},  {"icp5", 32},  {"ic11", 32},  {"ic12", 64},  {"ic07", 128},
  {"ic13", 256}, {"ic08", 256}, {"ic14", 512}, {"ic09", 512}, {"ic10", 1024},
};

constexpr quint32 ICO_HEADER_BYTES = 6;
constexpr quint32 ICO_ENTRY_BYTES = 16;
constexpr quint32 ICNS_CHUNK_HEADER_BYTES = 8;

void putLe16(QByteArray& out, quint16 value) {
  out.append(char(value & 0xFF));
  out.append(char((value >> 8) & 0xFF));
}

void putLe32(QByteArray& out, quint32 value) {
  for (int shift = 0; shift < 32; shift += 8) out.append(char((value >> shift) & 0xFF));
}

void putBe32(QByteArray& out, quint32 value) {
  for (int shift = 24; shift >= 0; shift -= 8) out.append(char((value >> shift) & 0xFF));
}

QByteArray renderPng(QSvgRenderer& svg, int px) {
  QImage image(px, px, QImage::Format_ARGB32);
  image.setDotsPerMeterX(DOTS_PER_METRE);
  image.setDotsPerMeterY(DOTS_PER_METRE);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  svg.render(&painter);
  painter.end();

  QByteArray png;
  QBuffer buffer(&png);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");
  return png;
}

QByteArray buildIco(QSvgRenderer& svg) {
  QList<QByteArray> frames;
  for (const int px : ICO_SIZES) frames.append(renderPng(svg, px));

  QByteArray out;
  putLe16(out, 0);
  putLe16(out, 1);   // 1 = icon, 2 = cursor
  putLe16(out, quint16(frames.size()));

  quint32 offset = ICO_HEADER_BYTES + ICO_ENTRY_BYTES * quint32(frames.size());
  for (int i = 0; i < frames.size(); ++i) {
    // Width and height are one byte each, so a 256-pixel frame is spelled 0.
    const char side = char(ICO_SIZES[i] >= 256 ? 0 : ICO_SIZES[i]);
    out.append(side);
    out.append(side);
    out.append(char(0));   // palette entries, 0 = truecolour
    out.append(char(0));
    putLe16(out, 1);       // colour planes
    putLe16(out, 32);      // bits per pixel
    putLe32(out, quint32(frames.at(i).size()));
    putLe32(out, offset);
    offset += quint32(frames.at(i).size());
  }
  for (const QByteArray& frame : frames) out.append(frame);
  return out;
}

QByteArray buildIcns(QSvgRenderer& svg) {
  QMap<int, QByteArray> pngs;
  for (const IcnsSlot& slot : ICNS_SLOTS)
    if (!pngs.contains(slot.px)) pngs.insert(slot.px, renderPng(svg, slot.px));

  QByteArray body;
  for (const IcnsSlot& slot : ICNS_SLOTS) {
    const QByteArray& png = pngs.value(slot.px);
    body.append(slot.type, 4);
    putBe32(body, quint32(png.size()) + ICNS_CHUNK_HEADER_BYTES);
    body.append(png);
  }

  QByteArray out("icns", 4);
  putBe32(out, quint32(body.size()) + ICNS_CHUNK_HEADER_BYTES);
  out.append(body);
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  // QCoreApplication, not QGuiApplication: the artwork carries no text, so painting it needs
  // no font database and the build machine needs no QPA platform plugin.
  QCoreApplication app(argc, argv);
  const QStringList args = QCoreApplication::arguments();
  if (args.size() < 3 || args.size() > 4) {
    std::fprintf(stderr, "usage: stencil_mkicon <input.svg> <output.ico|.icns|.png> [size]\n");
    return 2;
  }

  QSvgRenderer svg(args.at(1));
  if (!svg.isValid()) {
    std::fprintf(stderr, "mkicon: cannot read %s\n", qUtf8Printable(args.at(1)));
    return 2;
  }

  const QString suffix = QFileInfo(args.at(2)).suffix().toLower();
  QByteArray blob;
  if (suffix == QLatin1String("ico")) {
    blob = buildIco(svg);
  } else if (suffix == QLatin1String("icns")) {
    blob = buildIcns(svg);
  } else if (suffix == QLatin1String("png")) {
    bool ok = false;
    const int px = args.size() == 4 ? args.at(3).toInt(&ok) : 0;
    if (!ok || px <= 0) {
      std::fprintf(stderr, "mkicon: a .png output needs a pixel size\n");
      return 2;
    }
    blob = renderPng(svg, px);
  } else {
    std::fprintf(stderr, "mkicon: unknown output type '%s'\n", qUtf8Printable(suffix));
    return 2;
  }

  QFile out(args.at(2));
  if (!out.open(QIODevice::WriteOnly)) {
    std::fprintf(stderr, "mkicon: cannot write %s\n", qUtf8Printable(args.at(2)));
    return 3;
  }
  out.write(blob);
  out.close();

  std::printf("wrote %s (%lld bytes)\n", qUtf8Printable(args.at(2)), qint64(blob.size()));
  return 0;
}
