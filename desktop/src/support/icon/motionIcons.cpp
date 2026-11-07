#include "motionIcons.hpp"
#include "motionIconPainters.hpp"
#include "motionPrefs.hpp"
#include "skin/SkinLookStrategy.hpp"

namespace stencil::support {

  namespace {

    // mode → its hover keyframes' length (ms, before MOTION_ICON_SPEEDUP) and its glyph.
    struct MotionIconRow {
      const char* mode;
      double ms;
      void (*paint)(QPainter&, const QColor&, double);
    };
    const MotionIconRow ROWS[] = {
        {"none", 450, mmpaint::paintNone},     {"slide", 450, mmpaint::paintSlide},   {"transition", 450, mmpaint::paintTransition},
        {"water", 1125, mmpaint::paintWater},  {"fire", 900, mmpaint::paintFire},
        {"particles", 825 + 4 * 90, mmpaint::paintParticles},
    };

    // An unknown mode times as a 450ms one and paints as dust.
    const MotionIconRow& rowFor(const QString& mode) {
      static const MotionIconRow fallback{"", 450, mmpaint::paintParticles};
      for (const MotionIconRow& row : ROWS)
        if (mode == QLatin1String(row.mode)) return row;
      return fallback;
    }

  }  // namespace

  double motionIconMs(const QString& mode) { return rowFor(mode).ms / MOTION_ICON_SPEEDUP; }

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
    rowFor(mode).paint(p, colour, t);
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
      if (support::motionElapsed(clock.elapsed(), scale) > HOVER_MS) tick.stop();
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
    const double ms = hovered ? double(support::motionElapsed(clock.elapsed(), scale)) : 1e9;
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
    if (!support::skinTraits().hasIconMotion) return QStyledItemDelegate::eventFilter(watched, e);
    if (watched == view->viewport() && (e->type() == QEvent::MouseMove || e->type() == QEvent::HoverMove)) {
      const QPoint pos = e->type() == QEvent::MouseMove ? static_cast<QMouseEvent*>(e)->pos()
                                                        : static_cast<QHoverEvent*>(e)->position().toPoint();
      const int row = view->indexAt(pos).row();
      if (row != hoverRow) { hoverRow = row; clock.restart(); scale = support::motionScale(); tick.start(); }
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
    if (!support::skinTraits().hasIconMotion) return;
    clock.restart();
    scale = support::motionScale();
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
    const double ms = support::motionElapsed(clock.elapsed(), scale);
    const bool done = ms >= motionIconMs(mode);
    const double dpr = combo->devicePixelRatioF();
    combo->setItemIcon(row, motionIconFrame(mode, combo->palette().color(QPalette::Text),
                                            done ? 1e9 : ms, 16, dpr));
    if (done) tick.stop();
  }

}  // namespace stencil::support
