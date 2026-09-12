#include "appTooltip.hpp"

namespace stencil::gui {

  AppTooltip::AppTooltip(QWidget* parent) : QFrame(parent, Qt::ToolTip) {
    setObjectName(QString::fromLatin1(kObjectName));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    body_ = new TipBody(this);
    body_->setTextFormat(Qt::RichText);
    body_->setObjectName(QStringLiteral("stencilAppTooltipBody"));
    // Wraps at the browser's own tooltip ceiling (#app-tooltip max-width: 380px). Without
    // one a long sentence rendered as a single line the width of the screen instead of a
    // few readable ones.
    body_->setWordWrap(true);
    body_->setMaximumWidth(kMaxTipWidth);
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


  // Show `owner`'s tooltip near `globalPos`. Rich text is used as given; a plain string
  // goes through the same rendering Qt's tooltip would have shown. `originGlobal` is
  // where the tip's dust forms out of; left invalid it is the owner's centre, which is
  // wrong for an item view, whose owner is the whole viewport.
  void AppTooltip::showFor(QWidget* owner, const QString& text, const QPoint& globalPos,
                           const QRect& originGlobal) {
    origin_ = originGlobal;
    // The html pins its own width, so it is rendered HERE, in the type this body draws
    // with. The owner's tooltip was already rendered when it was set (main.cpp's
    // ToolTipChange filter) — measured in QToolTip's font, 11pt on macOS against the
    // body's 13pt, which broke "Image Filter" and a ⇧⌘X chord onto two lines. Every
    // enriched tooltip remembers the plain text it came from, so it is redone from that.
    body_->ensurePolished();
    const QFont font = body_->font();
    const QVariant plain = owner ? owner->property(kPlainTipProperty) : QVariant();
    const QString rich = plain.isValid()                    ? enrichedToolTip(plain.toString(), &font)
                         : text.trimmed().startsWith('<') ? text
                                                           : enrichedToolTip(text, &font);
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
        // …and may not MOVE meanwhile: the cloud was aimed where the tip was placed, so
        // one tracking the cursor mid-flight would leave its own sand behind. It picks
        // the cursor up again the moment it lands (browser controlTooltip.js).
        placeHold_.setRemainingTime(kDustInMs);
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
  // Slide an already-shown tip to a new cursor position: no re-measure, no entrance, no
  // fade. showFor would re-run its appearance bookkeeping on every mouse move.
  void AppTooltip::moveTo(const QPoint& globalPos) {
    if (isVisible() && !closing_ && placeHold_.hasExpired()) place(globalPos);
  }

  void AppTooltip::hideTip() {
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
  void AppTooltip::shakeKeys() {
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


  // Fly the tooltip's own motes out of — or back into — the control it describes.
  // Measured in that control's window (escapeHost lets the cloud past its edge, as a
  // tip near it goes); without one, or a box too small to grain, the fade above stands in.
  bool AppTooltip::dust(bool gather) {
    QWidget* owner = owner_.data();
    if (!owner || !owner->isVisible()) return false;
    // paintNow on a close: the panel hands over in one 60ms beat, and a deferred
    // first frame was exactly the gap in which the tip blinked out mote-less.
    return flyTipDust(this, owner->window(),
                      origin_.isValid() ? origin_.center()
                                        : owner->mapToGlobal(owner->rect().center()), gather,
                      gather ? kDustInMs : kDustOutMs,
                      /*escapeHost=*/true, /*paintNow=*/!gather)
           != nullptr;
  }

  void AppTooltip::place(const QPoint& cursor) {
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
  void AppTooltip::settleShake() {
    if (shakeDelay_) shakeDelay_->stop();
    if (shake_) shake_->stop();
    body_->settle();
  }


  // The one-shot that holds the nudge back until the motes have landed.
  QTimer* AppTooltip::shakeDelay() {
    if (!shakeDelay_) {
      shakeDelay_ = new QTimer(this);
      shakeDelay_->setSingleShot(true);
      QObject::connect(shakeDelay_, &QTimer::timeout, this, [this] { shakeKeys(); });
    }
    return shakeDelay_;
  }
}  // namespace stencil::gui
