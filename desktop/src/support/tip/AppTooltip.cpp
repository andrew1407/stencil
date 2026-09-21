#include "AppTooltip.hpp"

namespace stencil::gui {


  // New content: settled, and the cap hunt has to run again.
  void TipBody::setTip(const QString& rich) {
    settle();
    tip = rich;
    setText(rich);
    caps.clear();
    pieces.clear();
    hunted = false;
  }


  // 0 = nothing to shake.
  int TipBody::capCount() {
    if (!hunted) { hunted = true; findCaps(); }
    return int(caps.size());
  }


  // 0 or 1 is the resting slot.
  void TipBody::setShake(double t) {
    double x = 0, deg = 0;
    if (t > 0.0 && t < 1.0 && !caps.isEmpty()) {
      int i = 0;
      while (i < STOPS - 2 && t > STOP_T[i + 1]) i++;
      const double u = ease().valueForProgress((t - STOP_T[i]) / (STOP_T[i + 1] - STOP_T[i]));
      x = STOP_X[i] + (STOP_X[i + 1] - STOP_X[i]) * u;
      deg = STOP_DEG[i] + (STOP_DEG[i + 1] - STOP_DEG[i]) * u;
    }
    const int px = qRound(x);
    if (px == dx && qFuzzyCompare(deg + 1.0, this->deg + 1.0)) return;
    dx = px;
    this->deg = deg;
    update();
  }

  void TipBody::paintEvent(QPaintEvent* e) {
    if (caps.isEmpty() || (dx == 0 && qFuzzyIsNull(deg))) { QLabel::paintEvent(e); return; }
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QRegion holes;
    for (const QRect& r : caps) holes += r;
    p.setClipRegion(QRegion(rect()) - holes);   // the caps' slots stay empty
    p.drawPixmap(0, 0, flat);
    p.setClipping(false);
    for (int i = 0; i < caps.size(); i++) {
      const QPointF c = QRectF(caps[i]).center();
      p.save();
      p.translate(c + QPointF(dx, 0));
      p.rotate(deg);
      p.translate(-c);
      p.drawPixmap(caps[i].topLeft(), pieces[i]);
      p.restore();
    }
  }


  // Render with and without the cap faces; the pixels that differ are the caps.
  void TipBody::findCaps() {
    const QString bare = blankKeycaps(tip);
    if (bare.isEmpty() || width() <= 0 || height() <= 0) return;
    flat = grab();
    setText(bare);
    const QImage without = grab().toImage().convertToFormat(QImage::Format_ARGB32);
    setText(tip);
    const QImage with = flat.toImage().convertToFormat(QImage::Format_ARGB32);
    if (with.isNull() || with.size() != without.size()) return;
    const int w = with.width(), h = with.height();
    const qreal dpr = flat.devicePixelRatio() > 0 ? flat.devicePixelRatio() : 1.0;
    std::vector<char> diff(size_t(w) * h, 0);
    for (int y = 0; y < h; y++) {
      const auto* a = reinterpret_cast<const QRgb*>(with.constScanLine(y));
      const auto* b = reinterpret_cast<const QRgb*>(without.constScanLine(y));
      for (int x = 0; x < w; x++)
        if (a[x] != b[x]) diff[size_t(y) * w + x] = 1;
    }
    auto rowHas = [&](int y) {
      for (int x = 0; x < w; x++) if (diff[size_t(y) * w + x]) return true;
      return false;
    };
    auto colHas = [&](int x, int y0, int y1) {
      for (int y = y0; y <= y1; y++) if (diff[size_t(y) * w + x]) return true;
      return false;
    };
    // Bands of rows are the tip's lines; runs of columns inside one are its caps.
    for (int y0 = 0; y0 < h;) {
      if (!rowHas(y0)) { y0++; continue; }
      int y1 = y0;
      while (y1 + 1 < h && rowHas(y1 + 1)) y1++;
      for (int x0 = 0; x0 < w;) {
        if (!colHas(x0, y0, y1)) { x0++; continue; }
        int x1 = x0;
        while (x1 + 1 < w && colHas(x1 + 1, y0, y1)) x1++;
        const QRect r = QRectF(x0 / dpr, y0 / dpr, (x1 - x0 + 1) / dpr, (y1 - y0 + 1) / dpr)
                            .toAlignedRect()
                            .intersected(rect());   // rounding never reaches past the label
        caps << r;
        pieces << cut(r, dpr);
        x0 = x1 + 1;
      }
      y0 = y1 + 1;
    }
  }

  QPixmap TipBody::cut(const QRect& r, qreal dpr) const {
    QPixmap piece = flat.copy(QRect(QPoint(qRound(r.x() * dpr), qRound(r.y() * dpr)),
                                     QSize(qRound(r.width() * dpr), qRound(r.height() * dpr))));
    piece.setDevicePixelRatio(dpr);
    return piece;
  }
}  // namespace stencil::gui
