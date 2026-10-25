#pragma once

// Whole-file bytes in and out, and a picture as the PNG bytes the server stores.

#include <QBuffer>
#include <QByteArray>
#include <QFile>
#include <QImage>
#include <QString>

namespace stencil::gui {

  // The server is codec-free: it gets PNG bytes + the dimensions separately.
  inline QByteArray pngBytes(const QImage& img) {
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return out;
  }

  inline bool readFileBytes(const QString& path, QByteArray& out) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    out = f.readAll();
    return true;
  }
  inline bool writeFileBytes(const QString& path, const QByteArray& data) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(data);
    return true;
  }

}  // namespace stencil::gui
