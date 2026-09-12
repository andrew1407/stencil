#pragma once
// The transfer controller's clock, salt and PNG encode, private to the projectTransfer*.cpp TUs.
#include <QByteArray>
#include <QRandomGenerator>
#include <QBuffer>
#include <QDateTime>
#include <QImage>
#include <QString>

namespace stencil::gui {

  inline long long nowMs() { return QDateTime::currentMSecsSinceEpoch(); }
  inline std::string makeSalt() {
    return QString::number(QRandomGenerator::global()->bounded(1 << 24), 36).toStdString();
  }
  // The server is codec-free, so the desktop hands it encoded bytes + dimensions separately.
  inline QByteArray pngBytes(const QImage& img) {
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return out;
  }

}  // namespace stencil::gui
