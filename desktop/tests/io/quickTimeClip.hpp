#pragma once
// A QuickTime clip of Photo-JPEG frames, muxed by hand: a real video for a suite with no encoder,
// tool or binary fixture behind it. Each box carries only the fields a demuxer reads.
#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QSize>
#include <QtEndian>

namespace stencil::test {

  namespace qtClip {
    inline QByteArray be16(quint16 v) {
      QByteArray b(2, '\0');
      qToBigEndian(v, b.data());
      return b;
    }
    inline QByteArray be32(quint32 v) {
      QByteArray b(4, '\0');
      qToBigEndian(v, b.data());
      return b;
    }
    inline QByteArray zeros(int n) { return QByteArray(n, '\0'); }
    inline QByteArray box(const char* type, const QByteArray& body) {
      return be32(quint32(8 + body.size())) + QByteArray(type, 4) + body;
    }
    // A box whose body opens with a version byte and 24 bits of flags.
    inline QByteArray fullBox(const char* type, quint32 flags, const QByteArray& body) {
      return box(type, be32(flags) + body);
    }
    // The identity transform: 16.16 fixed point, with 2.30 in the last column.
    inline QByteArray unityMatrix() {
      return be32(0x10000) + zeros(12) + be32(0x10000) + zeros(12) + be32(0x40000000);
    }
    inline QByteArray jpegFrame(QSize size, int i) {
      QImage img(size, QImage::Format_RGB32);
      img.fill(QColor::fromHsv(i * 36 % 360, 200, 220));
      QByteArray jpg;
      QBuffer buf(&jpg);
      buf.open(QIODevice::WriteOnly);
      img.save(&buf, "JPG");
      return jpg;
    }
  }  // namespace qtClip

  // `frames` frames at 10 fps, one colour each; the samples are one chunk right after ftyp.
  inline QByteArray quickTimeClip(QSize size, int frames) {
    using namespace qtClip;
    constexpr quint32 SCALE = 600, DELTA = 60;   // time units per second, per frame
    const quint32 duration = quint32(frames) * DELTA;
    const quint16 w = quint16(size.width()), h = quint16(size.height());
    QByteArray samples, sizes;
    for (int i = 0; i < frames; ++i) {
      const QByteArray jpg = jpegFrame(size, i);
      samples += jpg;
      sizes += be32(quint32(jpg.size()));
    }
    const QByteArray ftyp = box("ftyp", QByteArray("qt  ") + be32(0x200) + QByteArray("qt  "));
    const QByteArray entry = box("jpeg", zeros(6) + be16(1) + zeros(8) + be32(0) + be32(512) + be16(w) +
                                             be16(h) + be32(72u << 16) + be32(72u << 16) + be32(0) + be16(1) +
                                             zeros(32) + be16(24) + be16(0xFFFF));
    const QByteArray stbl = box(
        "stbl", fullBox("stsd", 0, be32(1) + entry) +
                    fullBox("stts", 0, be32(1) + be32(quint32(frames)) + be32(DELTA)) +
                    fullBox("stsc", 0, be32(1) + be32(1) + be32(quint32(frames)) + be32(1)) +
                    fullBox("stsz", 0, be32(0) + be32(quint32(frames)) + sizes) +
                    fullBox("stco", 0, be32(1) + be32(quint32(ftyp.size() + 8))));
    const QByteArray minf =
        box("minf", fullBox("vmhd", 1, zeros(8)) +
                        box("dinf", fullBox("dref", 0, be32(1) + fullBox("alis", 1, {}))) + stbl);
    const QByteArray mdia =
        box("mdia", fullBox("mdhd", 0, zeros(8) + be32(SCALE) + be32(duration) + zeros(4)) +
                        fullBox("hdlr", 0, QByteArray("mhlrvide") + zeros(13)) + minf);
    const QByteArray tkhd = fullBox("tkhd", 3, zeros(8) + be32(1) + zeros(4) + be32(duration) + zeros(16) +
                                                   unityMatrix() + be32(quint32(w) << 16) + be32(quint32(h) << 16));
    const QByteArray mvhd = fullBox("mvhd", 0, zeros(8) + be32(SCALE) + be32(duration) + be32(0x10000) +
                                                   be16(0x100) + zeros(10) + unityMatrix() + zeros(24) + be32(2));
    return ftyp + box("mdat", samples) + box("moov", mvhd + box("trak", tkhd + mdia));
  }

}  // namespace stencil::test
