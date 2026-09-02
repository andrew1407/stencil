#pragma once
// The app's own control tooltip — the desktop port of #app-tooltip in
// browser/css/components.css, which fades over 90 ms instead of snapping.
//
// Qt's tooltip is a private QTipLabel: QSS has no transitions, and there is no supported
// hook to animate the label Qt shows. So QEvent::ToolTip is swallowed app-wide and this
// frameless panel is shown in its place. Qt still owns the TIMING — a ToolTip event only
// arrives after SH_ToolTip_WakeUpDelay (main.cpp pins it at 200 ms) — and the content is
// still tipContent's rendering, so nothing but the motion changes: the fade, plus one
// brief shake of the KEYCAPS as a tip carrying them appears, to point at the shortcut
// (browser/extension: .tip-key.key-shake). The panel itself never moves.
//
// Only a widget with its OWN non-empty toolTip() is taken over. Item views resolve
// per-index tooltips inside viewportEvent and have no widget tooltip of their own, so
// those keep Qt's path untouched.
//
// Header-only and Q_OBJECT-free (no signals/slots of its own), so it needs no MOC.
#include <QApplication>
#include <QCursor>
#include <QEasingCurve>
#include <QEvent>
#include <QFrame>
#include <QHelpEvent>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QList>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QRegion>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QtGlobal>

#include <vector>

#include "disintegrateOverlay.hpp"   // the shared surface dust
#include "modalReveal.hpp"   // support::motionReduced()
#include "tipContent.hpp"    // enrichedToolTip(), hasKeycaps(), blankKeycaps()

namespace stencil::gui {

  // The tooltip's body — the rendered rich text, and the keycaps' shake.
  //
  // The caps are painted PNGs inline in that ONE rich-text label (tipContent), not widgets,
  // so no layout can move them. They are LOCATED instead: the label is rendered twice, as
  // it is and with the cap faces blanked in boxes of the same size, and the pixels that
  // differ are the caps — wherever Qt's layout put them, caps mid-prose included. Each is
  // then blitted back at an offset with its resting slot clipped out. At rest the paint is
  // QLabel's own, untouched, so the settled tooltip renders exactly as it always did.
  class TipBody : public QLabel {
   public:
    // browser: @keyframes keycapShake (css/components.css) — one damped left/right flick.
    static constexpr int kStops = 6;
    static constexpr double kStopT[kStops] = {0.0, 0.15, 0.38, 0.62, 0.84, 1.0};
    static constexpr double kStopX[kStops] = {0.0, -3.0, 3.0, -2.0, 2.0, 0.0};
    static constexpr double kStopDeg[kStops] = {0.0, -3.0, 3.0, -2.0, 1.5, 0.0};

    explicit TipBody(QWidget* parent = nullptr) : QLabel(parent) {}

    // New content: settled, and the cap hunt has to run again.
    void setTip(const QString& rich) {
      settle();
      tip_ = rich;
      setText(rich);
      caps_.clear();
      pieces_.clear();
      hunted_ = false;
    }

    // How many caps this tooltip drew, hunting for them on first ask. 0 = nothing to shake.
    int capCount() {
      if (!hunted_) { hunted_ = true; findCaps(); }
      return int(caps_.size());
    }
    // Where the caps are along the flick, 0 at rest — what the tests watch.
    int capOffset() const { return dx_; }

    // Put the caps at `t` (0..1) along the keyframes; 0 or 1 is the resting slot.
    void setShake(double t) {
      double x = 0, deg = 0;
      if (t > 0.0 && t < 1.0 && !caps_.isEmpty()) {
        int i = 0;
        while (i < kStops - 2 && t > kStopT[i + 1]) i++;
        const double u = ease().valueForProgress((t - kStopT[i]) / (kStopT[i + 1] - kStopT[i]));
        x = kStopX[i] + (kStopX[i + 1] - kStopX[i]) * u;
        deg = kStopDeg[i] + (kStopDeg[i + 1] - kStopDeg[i]) * u;
      }
      const int px = qRound(x);
      if (px == dx_ && qFuzzyCompare(deg + 1.0, deg_ + 1.0)) return;
      dx_ = px;
      deg_ = deg;
      update();
    }
    void settle() { setShake(0.0); }

   protected:
    void paintEvent(QPaintEvent* e) override {
      if (caps_.isEmpty() || (dx_ == 0 && qFuzzyIsNull(deg_))) { QLabel::paintEvent(e); return; }
      QPainter p(this);
      p.setRenderHint(QPainter::SmoothPixmapTransform);
      QRegion holes;
      for (const QRect& r : caps_) holes += r;
      p.setClipRegion(QRegion(rect()) - holes);   // the caps' slots stay empty
      p.drawPixmap(0, 0, flat_);
      p.setClipping(false);
      for (int i = 0; i < caps_.size(); i++) {
        const QPointF c = QRectF(caps_[i]).center();
        p.save();
        p.translate(c + QPointF(dx_, 0));
        p.rotate(deg_);
        p.translate(-c);
        p.drawPixmap(caps_[i].topLeft(), pieces_[i]);
        p.restore();
      }
    }

   private:
    static const QEasingCurve& ease() {  // browser: cubic-bezier(0.36, 0.07, 0.19, 0.97)
      static const QEasingCurve c = [] {
        QEasingCurve e(QEasingCurve::BezierSpline);
        e.addCubicBezierSegment(QPointF(0.36, 0.07), QPointF(0.19, 0.97), QPointF(1, 1));
        return e;
      }();
      return c;
    }

    // Render with and without the cap faces; the pixels that differ are the caps.
    void findCaps() {
      const QString bare = blankKeycaps(tip_);
      if (bare.isEmpty() || width() <= 0 || height() <= 0) return;
      flat_ = grab();
      setText(bare);
      const QImage without = grab().toImage().convertToFormat(QImage::Format_ARGB32);
      setText(tip_);
      const QImage with = flat_.toImage().convertToFormat(QImage::Format_ARGB32);
      if (with.isNull() || with.size() != without.size()) return;
      const int w = with.width(), h = with.height();
      const qreal dpr = flat_.devicePixelRatio() > 0 ? flat_.devicePixelRatio() : 1.0;
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
      // Bands of rows are the tip's lines; runs of columns inside one are its caps (the
      // untouched "+" between two caps leaves a gap, so a chord splits cap by cap).
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
          caps_ << r;
          pieces_ << cut(r, dpr);
          x0 = x1 + 1;
        }
        y0 = y1 + 1;
      }
    }
    QPixmap cut(const QRect& r, qreal dpr) const {
      QPixmap piece = flat_.copy(QRect(QPoint(qRound(r.x() * dpr), qRound(r.y() * dpr)),
                                       QSize(qRound(r.width() * dpr), qRound(r.height() * dpr))));
      piece.setDevicePixelRatio(dpr);
      return piece;
    }

    QString tip_;
    QList<QRect> caps_;      // the caps' resting slots
    QList<QPixmap> pieces_;  // each cap, cut out of the settled render
    QPixmap flat_;           // the whole settled render
    bool hunted_ = false;
    int dx_ = 0;
    double deg_ = 0;
  };

  class AppTooltip : public QFrame {
   public:
    static constexpr int kFadeMs = 90;      // browser: #app-tooltip transition (the fallback)
    static constexpr int kShakeMs = 320;    // browser: keycapShake 0.32s, one per appearance
                                            // (TipBody holds its steps — the CAPS move, not this)
    // ── The tooltip is sand too (browser js/ui/controlTooltip.js) ──────────────
    // It forms from motes streaming out of the control it describes and comes apart into
    // motes pouring back into it — on the shared tip clock (disintegrateOverlay.hpp):
    // short, so a flight is over before a toolbar sweep reaches the next control.
    static constexpr int kDustInMs = kTipDustInMs;
    static constexpr int kDustOutMs = kTipDustOutMs;
    static constexpr int kDustHandOverMs = gui::kDustHandOverMs;
    static constexpr int kGap = 15;         // cursor offset, as Qt's own tooltip uses
    static constexpr const char* kObjectName = "stencilAppTooltip";

    explicit AppTooltip(QWidget* parent = nullptr) : QFrame(parent, Qt::ToolTip) {
      setObjectName(QString::fromLatin1(kObjectName));
      setAttribute(Qt::WA_TransparentForMouseEvents);
      setAttribute(Qt::WA_ShowWithoutActivating);
      setFocusPolicy(Qt::NoFocus);
      auto* lay = new QVBoxLayout(this);
      lay->setContentsMargins(0, 0, 0, 0);
      body_ = new TipBody(this);
      body_->setTextFormat(Qt::RichText);
      body_->setObjectName(QStringLiteral("stencilAppTooltipBody"));
      lay->addWidget(body_);
      hide();

      fade_ = new QVariantAnimation(this);
      fade_->setDuration(kFadeMs);
      QObject::connect(fade_, &QVariantAnimation::valueChanged, this,
                       [this](const QVariant& v) { setWindowOpacity(v.toDouble()); });
      QObject::connect(fade_, &QVariantAnimation::finished, this, [this] {
        if (closing_) { closing_ = false; QFrame::hide(); }
      });
      // Anti-stranding heartbeat: a fast pointer sweep can leave the owner without ever
      // sending a Leave we see (it is destroyed, re-laid-out, or the cursor jumps clear).
      // Whatever happened, the tooltip goes when the pointer is no longer on its owner.
      auto* beat = new QTimer(this);
      beat->setInterval(200);
      QObject::connect(beat, &QTimer::timeout, this, [this] {
        if (!isVisible() || closing_) return;
        if (!owner_ || !owner_->isVisible() || !owner_->window()->isActiveWindow()
            || !owner_->rect().contains(owner_->mapFromGlobal(QCursor::pos())))
          hideTip();
      });
      beat->start();
    }

    QWidget* owner() const { return owner_.data(); }

    // Show `owner`'s tooltip near `globalPos`. Rich text is used as given; a plain string
    // goes through the same rendering Qt's tooltip would have shown.
    void showFor(QWidget* owner, const QString& text, const QPoint& globalPos) {
      const QString rich = text.trimmed().startsWith('<') ? text : enrichedToolTip(text);
      if (rich.isEmpty()) { hideTip(); return; }
      bool dusted = false;
      // An APPEARANCE: a first show, one re-pointed at another control, or new content.
      // Qt keeps re-sending ToolTip while the pointer wanders inside one control (its own
      // label never appears, so its wake-up timer re-arms), and those must not re-shake.
      const bool appearing = !isVisible() || closing_ || owner != owner_ || rich != body_->text();
      settleShake();                   // never animate away from stale content
      owner_ = owner;
      body_->setTip(rich);
      adjustSize();
      place(globalPos);
      closing_ = false;
      fade_->stop();
      if (support::motionReduced()) {  // no fade; the end state, at once
        setWindowOpacity(1.0);
        show();
        raise();
      } else {
        const qreal from = isVisible() ? windowOpacity() : 0.0;
        setWindowOpacity(from);
        show();
        raise();
        // An APPEARANCE forms out of the control it describes; a re-send inside the same
        // control just carries on where it is.
        dusted = appearing && dust(true);
        if (dusted) {
          // The tip waits behind its own motes and fades up as the last of them land.
          setWindowOpacity(0.0);
          holdFadeKeys(fade_, kDustInMs);
        } else {
          fade_->setKeyValues({});
          fade_->setDuration(kFadeMs);
          fade_->setStartValue(from);
          fade_->setEndValue(1.0);
        }
        fade_->start();
      }
      // The point of the whole thing: caps on screen announce themselves as they arrive —
      // once they have ARRIVED. A nudge played while the tip is still assembling out of
      // its own motes is a movement nobody can see, which is the whole point of it, so
      // the dust route waits out the gather first.
      if (appearing && hasKeycaps(rich)) {
        if (dusted) shakeDelay()->start(kDustInMs);
        else shakeKeys();
      }
    }

    // Fade out and then hide. Idempotent, and a showFor() mid-fade takes it straight back
    // up from wherever it got to rather than blinking.
    void hideTip() {
      if (!isVisible()) { owner_.clear(); return; }
      settleShake();   // it fades out with its caps home, not mid-flick
      fade_->stop();
      if (support::motionReduced()) { owner_.clear(); closing_ = false; QFrame::hide(); return; }
      // Photographed and dusted while the owner is still known — the cloud is what the
      // tip leaves behind, so the panel itself hands over in one beat and goes.
      const bool dusted = dust(false);
      owner_.clear();
      closing_ = true;
      fade_->setKeyValues({});
      fade_->setDuration(dusted ? kDustHandOverMs : kFadeMs);
      fade_->setStartValue(windowOpacity());
      fade_->setEndValue(0.0);
      fade_->start();
    }

    // A brief attention shake as the tooltip appears — "and here is its shortcut". One
    // damped left-right pass over the KEYCAPS, never a loop, settling exactly on them.
    void shakeKeys() {
      if (shakeDelay_) shakeDelay_->stop();   // an explicit shake supersedes a queued one
      if (!isVisible() || support::motionReduced()) return;
      if (body_->capCount() == 0) return;   // nothing was drawn to move
      if (!shake_) {
        shake_ = new QVariantAnimation(this);
        shake_->setDuration(kShakeMs);
        shake_->setStartValue(0.0);
        shake_->setEndValue(1.0);
        QObject::connect(shake_, &QVariantAnimation::valueChanged, this,
                         [this](const QVariant& v) { body_->setShake(v.toDouble()); });
        QObject::connect(shake_, &QVariantAnimation::finished, this,
                         [this] { body_->settle(); });
      }
      shake_->stop();    // a pointer sweep restarts it on the new caps, never stacks
      body_->settle();
      shake_->start();
    }

    // The offset the caps are at, for tests: 0 when settled. The panel never moves.
    int shakeOffset() const { return body_->capOffset(); }
    int keycapsShown() const { return body_->capCount(); }
    bool shaking() const { return shake_ && shake_->state() == QAbstractAnimation::Running; }
    // Queued, but holding until the tip's own motes have landed — see showFor().
    bool shakePending() const { return shakeDelay_ && shakeDelay_->isActive(); }
    bool fadingOut() const { return closing_; }

   private:
    // Fly the tooltip's own motes out of — or back into — the control it describes.
    // Measured in that control's window (escapeHost lets the cloud past its edge, as a
    // tip near it goes); without one, or a box too small to grain, the fade above stands in.
    bool dust(bool gather) {
      QWidget* owner = owner_.data();
      if (!owner || !owner->isVisible()) return false;
      // paintNow on a close: the panel hands over in one 60ms beat, and a deferred
      // first frame was exactly the gap in which the tip blinked out mote-less.
      return flyTipDust(this, owner->window(),
                        owner->mapToGlobal(owner->rect().center()), gather,
                        gather ? kDustInMs : kDustOutMs,
                        /*escapeHost=*/true, /*paintNow=*/!gather)
             != nullptr;
    }

    void place(const QPoint& cursor) {
      const QScreen* scr = QGuiApplication::screenAt(cursor);
      if (!scr) scr = QGuiApplication::primaryScreen();
      const QRect avail = scr ? scr->availableGeometry() : QRect(0, 0, 1024, 768);
      int left = cursor.x() + kGap;
      int top = cursor.y() + kGap;
      if (left + width() > avail.right()) left = cursor.x() - width() - kGap;
      if (top + height() > avail.bottom()) top = cursor.y() - height() - kGap;
      left = qBound(avail.left() + 10, left, qMax(avail.left() + 10, avail.right() - width()));
      top = qBound(avail.top() + 10, top, qMax(avail.top() + 10, avail.bottom() - height()));
      move(left, top);
    }
    // Stop any shake — pending or playing — and put the caps back on their slots. A tip
    // dismissed or re-pointed mid-flight must never shake the caps of one already gone.
    void settleShake() {
      if (shakeDelay_) shakeDelay_->stop();
      if (shake_) shake_->stop();
      body_->settle();
    }

    // The one-shot that holds the nudge back until the motes have landed.
    QTimer* shakeDelay() {
      if (!shakeDelay_) {
        shakeDelay_ = new QTimer(this);
        shakeDelay_->setSingleShot(true);
        QObject::connect(shakeDelay_, &QTimer::timeout, this, [this] { shakeKeys(); });
      }
      return shakeDelay_;
    }

    TipBody* body_ = nullptr;
    QVariantAnimation* fade_ = nullptr;
    QVariantAnimation* shake_ = nullptr;
    QTimer* shakeDelay_ = nullptr;
    QPointer<QWidget> owner_;
    bool closing_ = false;
  };

  // The app-wide filter that hands QEvent::ToolTip to AppTooltip. Q_OBJECT-free for the
  // same reason as the panel: it only overrides eventFilter.
  class AppTooltipFilter : public QObject {
   public:
    explicit AppTooltipFilter(QObject* parent = nullptr) : QObject(parent) {}

    AppTooltip* tip() {
      if (!tip_) tip_ = new AppTooltip(nullptr);
      return tip_;
    }

   protected:
    bool eventFilter(QObject* o, QEvent* e) override {
      auto* w = qobject_cast<QWidget*>(o);
      switch (e->type()) {
        case QEvent::ToolTip: {
          // Only a widget carrying its OWN tooltip; anything else (item views resolving a
          // per-index tooltip in viewportEvent) keeps Qt's path.
          if (!w || w->toolTip().isEmpty()) break;
          tip()->showFor(w, w->toolTip(), static_cast<QHelpEvent*>(e)->globalPos());
          return true;   // Qt's own label must not also appear
        }
        case QEvent::Shortcut:
          // A LIVE shortcut never arrives as a key press — Qt consumes the key and sends
          // this instead — so it has to be dismissed from here.
          if (tip_) tip_->hideTip();
          break;
        case QEvent::KeyPress: {
          const auto* ke = static_cast<QKeyEvent*>(e);
          if (ke->isAutoRepeat()) break;
          // Every key retires the tooltip, Escape included — the shake announces the
          // shortcut while you read the tip, it is not a way to pin the tooltip open.
          if (tip_ && tip_->isVisible()) tip_->hideTip();
          break;
        }
        case QEvent::Leave:
        case QEvent::Hide:
        case QEvent::WindowDeactivate:
          if (tip_ && w && w == tip_->owner()) tip_->hideTip();
          break;
        case QEvent::MouseButtonPress:
        case QEvent::Wheel:
          if (tip_) tip_->hideTip();
          break;
        default:
          break;
      }
      return QObject::eventFilter(o, e);
    }

   private:
    AppTooltip* tip_ = nullptr;
  };

  // Install the fading tooltip on the running application. Idempotent — the filter and
  // the panel are process-wide, like Qt's own tooltip.
  inline AppTooltipFilter* installAppTooltips() {
    static QPointer<AppTooltipFilter> filter;
    if (!filter && qApp) {
      filter = new AppTooltipFilter(qApp);
      qApp->installEventFilter(filter);
    }
    return filter.data();
  }

  // The live panel, creating it on demand; null only with no QApplication.
  inline AppTooltip* appTooltip() {
    AppTooltipFilter* f = installAppTooltips();
    return f ? f->tip() : nullptr;
  }

}  // namespace stencil::gui
