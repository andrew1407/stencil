#include "motionIcons.hpp"

namespace stencil::support {

  double motionIconMs(const QString& mode) {
    double ms = 450;
    if (mode == QLatin1String("water")) ms = 1125;
    else if (mode == QLatin1String("fire")) ms = 900;
    else if (mode == QLatin1String("particles")) ms = 825 + 4 * 90;
    return ms / MOTION_ICON_SPEEDUP;
  }

  // `box` is square, any size — the drawing is 16 units; a big `ms` = at rest.
  void paintMotionIcon(QPainter& p, const QRectF& box, const QString& mode,
                       const QColor& colour, double ms) {
    const double s = box.width() / 16.0;
    const double t = std::clamp(ms / motionIconMs(mode), 0.0, 1.0);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.translate(box.topLeft());
    p.scale(s, s);
    QPen pen(colour, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    if (mode == QLatin1String("none")) {
      p.drawEllipse(QPointF(8, 8), 5.6, 5.6);
      // Browser stroke-dashoffset 11.4 → 0.
      const double k = mmEaseOut(t);
      const QPointF a(12.05, 3.95), b(3.95, 12.05);
      if (k > 0.01) p.drawLine(a, a + (b - a) * k);
    } else if (mode == QLatin1String("slide")) {
      // Browser mmShaft (stroke-dashoffset 9.9 → 0) over the first 35%; mmHead over the last third.
      const double ks = mmEaseOut(std::clamp(t / (0.35 / 0.45), 0.0, 1.0));
      const QPointF a(4.5, 11.5), b(11.5, 4.5);
      if (ks > 0.01) p.drawLine(a, a + (b - a) * ks);
      const double kh = std::clamp((t - 0.65) / 0.35, 0.0, 1.0);
      if (kh > 0.01) {
        p.setOpacity(mmEaseOut(kh));
        QPainterPath head;
        head.moveTo(6.5, 4.5); head.lineTo(11.5, 4.5); head.lineTo(11.5, 9.5);
        p.drawPath(head);
      }
    } else if (mode == QLatin1String("water")) {
      // 0% translateY(-9) scale(.6) fade → 70% landed squashed (1.06, .94) → 100% rest.
      double dy, sx, sy, op;
      if (t < 0.7) { const double k = mmOut(t / 0.7); dy = -9 * (1 - k); sx = mmLerp(0.6, 1.06, k); sy = mmLerp(0.6, 0.94, k); op = k; }
      else { const double k = mmOut((t - 0.7) / 0.3); dy = 0; sx = mmLerp(1.06, 1, k); sy = mmLerp(0.94, 1, k); op = 1; }
      p.setOpacity(op);
      p.translate(8, 8 + dy); p.scale(sx, sy); p.translate(-8, -8);
      QPainterPath drop;
      drop.moveTo(8, 2.4);
      drop.cubicTo(8, 2.4, 3.9, 7.4, 3.9, 10.1);
      drop.arcTo(QRectF(3.9, 6.0, 8.2, 8.2), 180, 180);
      drop.cubicTo(12.1, 7.4, 8, 2.4, 8, 2.4);
      drop.closeSubpath();
      p.setPen(Qt::NoPen); p.setBrush(colour);
      p.drawPath(drop);
    } else if (mode == QLatin1String("fire")) {
      // Keyframes: 0% scale(.5,.15) fade → 35% (1.05,.8) → 60% (.94,1.1) → 80% (1.04,.96) → 100% rest.
      struct K { double at, sx, sy, op; };
      const K keys[] = {{0, 0.5, 0.15, 0}, {0.35, 1.05, 0.8, 1}, {0.6, 0.94, 1.1, 1}, {0.8, 1.04, 0.96, 1}, {1, 1, 1, 1}};
      K a = keys[0], b = keys[1];
      for (int i = 1; i < 5; i++) if (t >= keys[i - 1].at) { a = keys[i - 1]; b = keys[i]; }
      const double k = b.at > a.at ? mmEaseOut((t - a.at) / (b.at - a.at)) : 1;
      p.setOpacity(mmLerp(a.op, b.op, k));
      // transform-origin 50% 100% of the flame's box (x 3.7..12.3, bottom y 13.6).
      p.translate(8, 13.6); p.scale(mmLerp(a.sx, b.sx, k), mmLerp(a.sy, b.sy, k)); p.translate(-8, -13.6);
      QPainterPath flame;
      flame.moveTo(8.2, 1.6);
      flame.cubicTo(9.4, 4.2, 12.3, 5.8, 12.3, 9.3);
      flame.arcTo(QRectF(3.7, 5.0, 8.6, 8.6), 0, -180);
      flame.cubicTo(3.7, 7.4, 5.0, 6.4, 5.6, 4.9);
      flame.cubicTo(6.4, 6.0, 6.9, 6.7, 7.4, 6.5);
      flame.cubicTo(7.0, 5.0, 7.5, 3.2, 8.2, 1.6);
      flame.closeSubpath();
      p.setPen(Qt::NoPen); p.setBrush(colour);
      p.drawPath(flame);
    } else {   // particles (dust): five specks settling in from the top, 60ms apart over 550ms
      const QPointF at[5] = {{4, 6}, {8.5, 4.2}, {12, 7}, {6.2, 10.5}, {10.4, 11.6}};
      const double r[5] = {1.3, 1.0, 1.2, 1.1, 1.35};
      p.setPen(Qt::NoPen); p.setBrush(colour);
      for (int i = 0; i < 5; i++) {
        // Browser mmDust + nth-of-type delays: 90ms apart, 825ms each, `t` over the whole 1185ms.
        const double start = i * 90 / 1185.0, span = 825 / 1185.0;
        const double k = mmOut(std::clamp((t - start) / span, 0.0, 1.0));
        p.setOpacity(k);
        p.drawEllipse(at[i] + QPointF(0, -7 * (1 - k)), r[i], r[i]);
      }
    }
    p.restore();
  }

  QIcon motionIconFrame(const QString& mode, const QColor& colour, double ms, int px, double dpr) {
    QPixmap pm(int(px * dpr), int(px * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    paintMotionIcon(p, QRectF(0, 0, px, px), mode, colour, ms);
    p.end();
    return QIcon(pm);
  }

  QIcon motionModeIcon(const QString& mode, const QColor& colour, int px, double dpr) {
    return motionIconFrame(mode, colour, 1e9, px, dpr);
  }

  MotionIconDelegate::MotionIconDelegate(QAbstractItemView* view,
                                         QObject* parent) : QStyledItemDelegate(parent), view(view) {
    this->view->viewport()->installEventFilter(this);
    tick.setInterval(16);
    QObject::connect(&tick, &QTimer::timeout, this->view->viewport(), [this] {
      if (clock.elapsed() > HOVER_MS) tick.stop();
      this->view->viewport()->update();
    });
  }

  void MotionIconDelegate::paint(QPainter* p, const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const {
    // The frame is handed to the style AS the row's icon, never painted beside it.
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    const QString mode = index.data(Qt::UserRole).toString();
    const bool hovered = index.row() == hoverRow;
    const double ms = hovered ? double(clock.elapsed()) : 1e9;
    // The browser's 16px, not PM_SmallIconSize (half again as big on a Mac).
    opt.decorationSize = QSize(MOTION_ICON_PX, MOTION_ICON_PX);
    const double dpr = p->device() ? p->device()->devicePixelRatio() : 1.0;
    // Always the row's TEXT colour: HighlightedText's white vanished on the light theme's accent wash.
    opt.icon = motionIconFrame(mode, opt.palette.color(QPalette::Text), ms, MOTION_ICON_PX, dpr);
    opt.features |= QStyleOptionViewItem::HasDecoration;
    const QWidget* w = opt.widget;
    QStyle* style = w ? w->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, p, w);
  }

  bool MotionIconDelegate::eventFilter(QObject* watched, QEvent* e) {
    if (watched == view->viewport() && (e->type() == QEvent::MouseMove || e->type() == QEvent::HoverMove)) {
      const QPoint pos = e->type() == QEvent::MouseMove ? static_cast<QMouseEvent*>(e)->pos()
                                                        : static_cast<QHoverEvent*>(e)->position().toPoint();
      const int row = view->indexAt(pos).row();
      if (row != hoverRow) { hoverRow = row; clock.restart(); tick.start(); }
    } else if (watched == view->viewport() && e->type() == QEvent::Leave) {
      hoverRow = -1;
      tick.stop();
      view->viewport()->update();
    }
    return QStyledItemDelegate::eventFilter(watched, e);
  }

  MotionIconFace::MotionIconFace(QComboBox* combo) : QObject(combo), combo(combo) {
    this->combo->installEventFilter(this);
    tick.setInterval(16);
    QObject::connect(&tick, &QTimer::timeout, this->combo, [this] { frame(); });
    QObject::connect(this->combo, &QComboBox::currentIndexChanged, this->combo, [this](int) { play(); });
  }

  void MotionIconFace::play() {
    clock.restart();
    tick.start();
    frame();
  }

  // A QIcon bakes its pixels, so a theme flip left these in the OLD ink; the popup rows
  // are painted live by MotionIconDelegate.
  void MotionIconFace::reink() {
    const QColor ink = combo->palette().color(QPalette::Text);
    if (ink == inked) return;   // …and only when it really moved: setItemIcon repaints
    inked = ink;
    const double dpr = combo->devicePixelRatioF();
    for (int i = 0; i < combo->count(); ++i)
      combo->setItemIcon(i, motionIconFrame(combo->itemData(i).toString(), ink, 1e9,
                                             MOTION_ICON_PX, dpr));
  }

  bool MotionIconFace::eventFilter(QObject* watched, QEvent* e) {
    if (watched == combo) {
      if (e->type() == QEvent::Enter) play();
      else if (e->type() == QEvent::PaletteChange) reink();
    }
    return QObject::eventFilter(watched, e);
  }

  void MotionIconFace::frame() {
    const int row = combo->currentIndex();
    if (row < 0) { tick.stop(); return; }
    const QString mode = combo->itemData(row).toString();
    const double ms = clock.elapsed();
    const bool done = ms >= motionIconMs(mode);
    const double dpr = combo->devicePixelRatioF();
    combo->setItemIcon(row, motionIconFrame(mode, combo->palette().color(QPalette::Text),
                                            done ? 1e9 : ms, 16, dpr));
    if (done) tick.stop();
  }

}  // namespace stencil::support
