#include "stripPaint.hpp"

#include <QTransform>

#include <algorithm>
#include <cmath>

namespace stencil::support::genie {

  QRectF mapped(const Strip& from, const QRectF& s) {
    const double kx = from.target.width() / from.source.width(), ky = from.target.height() / from.source.height();
    return QRectF(from.target.x() + (s.x() - from.source.x()) * kx, from.target.y() + (s.y() - from.source.y()) * ky,
                  s.width() * kx, s.height() * ky);
  }

  bool within(const QRectF& a, const QRectF& b, double tol) {
    return std::abs(a.left() - b.left()) <= tol && std::abs(a.right() - b.right()) <= tol
           && std::abs(a.top() - b.top()) <= tol && std::abs(a.bottom() - b.bottom()) <= tol;
  }

  void mergeStrips(const QVector<Strip>& in, QVector<Strip>& out, double tol) {
    out.clear();
    for (qsizetype i = 0; i < in.size();) {
      Strip run = in[i];
      qsizetype j = i + 1;
      for (; j < in.size(); ++j) {
        const Strip next{run.target | in[j].target, run.source | in[j].source};
        bool isFit = next.source.width() > 0 && next.source.height() > 0;
        for (qsizetype m = i; isFit && m <= j; ++m) isFit = within(mapped(next, in[m].source), in[m].target, tol);
        if (!isFit) break;
        run = next;
      }
      out.push_back(run);
      i = j;
    }
  }

  void transposeInto(const QVector<Strip>& in, QVector<Strip>& out) {
    out.resize(in.size());
    for (qsizetype k = 0; k < in.size(); ++k) out[k] = {transposed(in[k].target), transposed(in[k].source)};
  }

  QImage transposed(const QImage& img) {
    QImage out = img.transformed(QTransform().rotate(90)).flipped(Qt::Horizontal);
    out.setDevicePixelRatio(img.devicePixelRatio());
    return out;
  }

  namespace {
    // The band `s` lands on, in p's coordinates, overlapped a quarter pixel so no seam shows at a fractional
    // scale; null when it misses `clip`.
    QRectF stripTarget(const Strip& s, QPointF origin, const QRectF& clip) {
      const QRectF at = s.target.translated(-origin).adjusted(-0.25, -0.25, 0.25, 0.25);
      return at.intersects(clip) ? at : QRectF();
    }
    QRectF stripSource(const Strip& s, const QSizeF& px) {
      return QRectF(s.source.x() * px.width(), s.source.y() * px.height(), s.source.width() * px.width(), s.source.height() * px.height());
    }
  }  // namespace

  void drawStrips(QPainter& p, const QImage& shot, const QVector<Strip>& strips, QPointF origin, const QRectF& clip) {
    for (const Strip& s : strips)
      if (const QRectF at = stripTarget(s, origin, clip); !at.isNull()) p.drawImage(at, shot, stripSource(s, shot.size()));
  }

  void drawStrips(QPainter& p, const QPixmap& shot, const QVector<Strip>& strips, QPointF origin, const QRectF& clip) {
    for (const Strip& s : strips)
      if (const QRectF at = stripTarget(s, origin, clip); !at.isNull()) p.drawPixmap(at, shot, stripSource(s, shot.size()));
  }

  QVector<QRect> columnsOf(const QRegion& dirty) {
    QVector<QRect> rects(dirty.begin(), dirty.end()), out;
    std::sort(rects.begin(), rects.end(), [](const QRect& a, const QRect& b) { return a.left() < b.left(); });
    for (const QRect& r : rects)
      if (!out.isEmpty() && r.left() <= out.back().right() + 1) out.back() |= r;
      else out.push_back(r);
    return out;
  }

  void drawStripsAcross(QPainter& p, const QImage& shotT, const QVector<Strip>& stripsT, QPointF origin,
                        const QRegion& dirty, qreal dpr, QImage* scratch) {
    QImage own;
    QImage& buf = scratch ? *scratch : own;
    for (const QRect& r : columnsOf(dirty)) {
      const QSize need(static_cast<int>(std::ceil(r.height() * dpr)), static_cast<int>(std::ceil(r.width() * dpr)));
      if (buf.size() != need) buf = QImage(need, QImage::Format_ARGB32_Premultiplied);
      buf.setDevicePixelRatio(dpr);
      buf.fill(Qt::transparent);
      {
        QPainter bp(&buf);
        bp.setRenderHint(QPainter::SmoothPixmapTransform);
        drawStrips(bp, shotT, stripsT, QPointF(origin.y() + r.y(), origin.x() + r.x()), QRectF(0, 0, r.height(), r.width()));
      }
      p.drawImage(r.topLeft(), transposed(buf));
    }
  }

}  // namespace stencil::support::genie
