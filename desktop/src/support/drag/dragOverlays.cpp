#include "dragOverlays.hpp"
#include "iconDrag.hpp"
#include "motionPrefs.hpp"
#include "theme.hpp"

#include <QHash>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <vector>

namespace stencil::support {

  namespace {
    constexpr double GHOST_OPACITY = 0.85;   // the browser's GHOST_OPACITY (ui/drag/iconDrag.js)
    constexpr int GLOW_PAD = 18;             // px the glow stands off the target: the widest blur (18px)
    constexpr int GLOW_RADIUS = 7;
    constexpr int SHINE_BEAT_MS = 1400;      // one breath of the ghost's rim (css icon/drag.css)
    constexpr int FRAME_MS = 16;
    constexpr double SOURCE_OPACITY = 0.45;  // .icon-drag-source opacity; its dashed outline sits 2px off

    // css `box-shadow: 0 0 0 <width> <ring>, 0 0 <blur> <haze>`: a solid ring, then a blur whose
    // alpha starts at half the haze's at the edge and falls off over `blur` px.
    void paintShadow(QPainter& p, const QRectF& box, QColor colour, double width, double ring,
                     double blur, double haze) {
      p.setBrush(Qt::NoBrush);
      for (int i = int(std::ceil(blur)); i >= 1; --i) {
        const double fall = 1.0 - double(i) / (blur + 1);
        colour.setAlphaF(haze * 0.5 * fall * fall);
        p.setPen(QPen(colour, 1.0));
        const QRectF r = box.adjusted(-width - i + 0.5, -width - i + 0.5, width + i - 0.5, width + i - 0.5);
        p.drawRoundedRect(r, GLOW_RADIUS + width + i, GLOW_RADIUS + width + i);
      }
      colour.setAlphaF(ring);
      p.setPen(QPen(colour, width));
      const double h = width / 2;
      p.drawRoundedRect(box.adjusted(-h, -h, h, h), GLOW_RADIUS + h, GLOW_RADIUS + h);
    }

    // Keyed by target; an entry leaves with its target, taking the glow along.
    QHash<const QWidget*, QPointer<DropGlow>>& glows() {
      static QHash<const QWidget*, QPointer<DropGlow>> live;
      return live;
    }

    // One pass of a box blur over a `w` x `h` alpha plane, along x or y; three make a near-gaussian.
    void boxBlurAlpha(std::vector<int>& a, int w, int h, int r, bool horizontal) {
      const int lines = horizontal ? h : w, len = horizontal ? w : h, step = horizontal ? 1 : w;
      std::vector<int> line(len);
      for (int l = 0; l < lines; ++l) {
        int* at = a.data() + (horizontal ? l * w : l);
        for (int i = 0; i < len; ++i) line[i] = at[i * step];
        int sum = 0;
        for (int i = -r; i <= r; ++i) sum += line[std::clamp(i, 0, len - 1)];
        for (int i = 0; i < len; ++i) {
          at[i * step] = sum / (2 * r + 1);
          sum += line[std::min(i + r + 1, len - 1)] - line[std::max(i - r, 0)];
        }
      }
    }

    // css `drop-shadow(0 0 <blur> <colour>)` of the face's own shape: its alpha, blurred (sigma = blur / 2)
    // and tinted, on a canvas `pad` px wider on every side.
    QPixmap markGlow(const QPixmap& face, const QColor& colour, double blur, int pad) {
      const qreal dpr = face.devicePixelRatio() > 0 ? face.devicePixelRatio() : 1.0;
      const QImage src = face.toImage().convertToFormat(QImage::Format_ARGB32);
      const int p = qRound(pad * dpr), w = src.width() + 2 * p, h = src.height() + 2 * p;
      std::vector<int> a(size_t(w) * h, 0);
      for (int y = 0; y < src.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        for (int x = 0; x < src.width(); ++x) a[size_t(y + p) * w + x + p] = qAlpha(row[x]);
      }
      const int r = std::max(1, qRound(blur / 2 * dpr));
      for (int pass = 0; pass < 3; ++pass) { boxBlurAlpha(a, w, h, r, true); boxBlurAlpha(a, w, h, r, false); }
      QImage glow(w, h, QImage::Format_ARGB32);
      for (int y = 0; y < h; ++y) {
        auto* row = reinterpret_cast<QRgb*>(glow.scanLine(y));
        for (int x = 0; x < w; ++x) row[x] = qRgba(colour.red(), colour.green(), colour.blue(), a[size_t(y) * w + x]);
      }
      QPixmap out = QPixmap::fromImage(glow);
      out.setDevicePixelRatio(dpr);
      return out;
    }
  }  // namespace

  double ghostShine(double ms, bool still) {
    return still ? 1.0 : 0.5 - 0.5 * std::cos(2 * M_PI * ms / SHINE_BEAT_MS);
  }

  // The widget stands GLOW_PAD off the face on every side, room for the rim's shine.
  DragGhost::DragGhost(QWidget* source, const QPoint& grab, const QPixmap& picture)
      : QWidget(source->window()), face(picture.isNull() ? source->grab() : picture),
        grab(grab + QPoint(GLOW_PAD, GLOW_PAD)), accent(source->palette().color(QPalette::Highlight)) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    // An owner's own art is a mark (browser svg.icon-drag-ghost): it shines along its drawn edge.
    if (!picture.isNull()) {
      glowNear = markGlow(face, accent, 2, GLOW_PAD);
      glowFar = markGlow(face, accent, 7, GLOW_PAD);
    }
    resize((QSizeF(face.size()) / face.devicePixelRatio()).toSize() + QSize(2 * GLOW_PAD, 2 * GLOW_PAD));
    follow(source->mapToGlobal(grab));
    clock.start();
    if (!motionReduced()) {
      connect(&ticker, &QTimer::timeout, this, qOverload<>(&QWidget::update));
      ticker.start(FRAME_MS);
    }
    show();
  }

  QSize DragGhost::faceSize() const { return size() - QSize(2 * GLOW_PAD, 2 * GLOW_PAD); }

  void DragGhost::follow(const QPoint& global) {
    move(parentWidget()->mapFromGlobal(global) - grab);
    raise();
  }

  void DragGhost::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const double beat = ghostShine(clock.elapsed(), motionReduced());
    if (!glowNear.isNull()) {
      // icon-ghost-shine-mark: the accent's 2px and 7px shadows breathing in; the face over them.
      p.setOpacity(GHOST_OPACITY * (0.45 + 0.55 * beat));
      p.drawPixmap(0, 0, glowNear);
      p.setOpacity(GHOST_OPACITY * beat);
      p.drawPixmap(0, 0, glowFar);
      p.setOpacity(GHOST_OPACITY);
      p.drawPixmap(GLOW_PAD, GLOW_PAD, face);
      return;
    }
    p.setOpacity(GHOST_OPACITY);
    p.drawPixmap(GLOW_PAD, GLOW_PAD, face);
    p.setOpacity(1.0);
    // icon-ghost-shine: from a 1.5px ring at 40% and a 4px haze at 25% to 90% and 12px at 60%.
    const QRectF box = QRectF(rect()).adjusted(GLOW_PAD, GLOW_PAD, -GLOW_PAD, -GLOW_PAD);
    paintShadow(p, box, accent, 1.5, 0.4 + 0.5 * beat, 4 + 8 * beat, 0.25 + 0.35 * beat);
  }

  DragSourceMark::DragSourceMark(QWidget* source, const QRect& held) : QWidget(source->window()), source(source) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    const QColor page = source->parentWidget() ? source->parentWidget()->palette().color(QPalette::Window)
                                               : palette().color(QPalette::Window);
    veil = page;
    veil.setAlphaF(1.0 - SOURCE_OPACITY);
    dash = gui::accentShade(source->palette().color(QPalette::Highlight), page.lightnessF() < 0.5);
    setGeometry(QRect(parentWidget()->mapFromGlobal(held.topLeft()), held.size()).adjusted(-3, -3, 3, 3));
    show();
    raise();
  }

  // The control seen through at 45%, its outline dashed 2px off its edge in --accent-2.
  void DragSourceMark::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF face = QRectF(rect()).adjusted(3, 3, -3, -3);
    p.setPen(Qt::NoPen);
    p.setBrush(veil);
    p.drawRoundedRect(face, GLOW_RADIUS, GLOW_RADIUS);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(dash, 1.0, Qt::DashLine));
    p.drawRoundedRect(face.adjusted(-2.5, -2.5, 2.5, 2.5), GLOW_RADIUS + 2, GLOW_RADIUS + 2);
  }

  DropGlow::DropGlow(QWidget* target) : QWidget(target->window()), target(target) {
    setObjectName(QLatin1String(DROP_GLOW_NAME));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    track();
    show();
  }

  void DropGlow::setOver(bool over) {
    if (this->over == over) return;
    this->over = over;
    update();
  }

  void DropGlow::track() {
    if (!target) return;
    const QRect r(target->mapTo(parentWidget(), QPoint(0, 0)), target->size());
    setGeometry(r.adjusted(-GLOW_PAD, -GLOW_PAD, GLOW_PAD, GLOW_PAD));
    raise();
  }

  // Concentric rounded rims fading outward: the browser's 2px ring plus its blurred shadow.
  void DropGlow::paintEvent(QPaintEvent*) {
    if (!target) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF box = QRectF(rect()).adjusted(GLOW_PAD, GLOW_PAD, -GLOW_PAD, -GLOW_PAD);
    // .icon-drop-target: a 2px ring at 45% and a 10px haze at 35%; under the pointer, full and 18px.
    const QColor accent = target->palette().color(QPalette::Highlight);
    if (over) paintShadow(p, box, accent, 2.0, 1.0, 18, 1.0);
    else paintShadow(p, box, accent, 2.0, 0.45, 10, 0.35);
  }

  void markDropTarget(QWidget* target, bool on, bool over) {
    if (!target) return;
    if (!on) {
      delete glows().take(target).data();
      return;
    }
    QPointer<DropGlow>& glow = glows()[target];
    if (!glow) {
      glow = new DropGlow(target);
      QObject::connect(target, &QObject::destroyed, glow, [target] { delete glows().take(target).data(); });
    }
    glow->setOver(over);
    glow->track();
  }

}  // namespace stencil::support
