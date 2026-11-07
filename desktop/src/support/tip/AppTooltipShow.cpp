#include "AppTooltip.hpp"
#include "tipFlight.hpp"
#include "../menu/popupSlide.hpp"
#include "../motionPrefs.hpp"
#include "../skin/SkinLookStrategy.hpp"

namespace stencil::gui {
  namespace {
    bool hasNonDustMotion() { return support::isSlideMotionOk() || support::isTransitionMotionOk(); }
  }  // namespace

  AppTooltip::AppTooltip(QWidget* parent) : QFrame(parent, Qt::ToolTip) {
    setObjectName(QString::fromLatin1(OBJECT_NAME));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    body = new TipBody(this);
    body->setTextFormat(Qt::RichText);
    body->setObjectName(QStringLiteral("stencilAppTooltipBody"));
    // Browser #app-tooltip max-width: 380px.
    body->setWordWrap(true);
    body->setMaximumWidth(MAX_TIP_WIDTH);
    lay->addWidget(body);
    hide();

    fade = new QVariantAnimation(this);
    fade->setDuration(FADE_MS);
    QObject::connect(fade, &QVariantAnimation::valueChanged, this,
                     [this](const QVariant& v) { setWindowOpacity(v.toDouble()); });
    QObject::connect(fade, &QVariantAnimation::finished, this, [this] {
      if (closing) { closing = false; QFrame::hide(); }
    });
    // Anti-stranding heartbeat: a fast sweep can leave the owner without a Leave we see.
    auto* beat = new QTimer(this);
    beat->setInterval(200);
    QObject::connect(beat, &QTimer::timeout, this, [this] {
      if (!isVisible() || closing) return;
      if (!owner || !owner->isVisible() || !owner->window()->isActiveWindow()
          || !owner->rect().contains(owner->mapFromGlobal(QCursor::pos())))
        hideTip();
    });
    beat->start();
  }

  // `originGlobal` invalid = the owner's centre (wrong for an item view's viewport).
  void AppTooltip::showFor(QWidget* owner, const QString& text, const QPoint& globalPos,
                           const QRect& originGlobal) {
    if (heldOff) return;
    origin = originGlobal;
    // Re-rendered from the plain text in THIS body's font: the owner's copy was measured
    // in QToolTip's (11pt on macOS vs 13pt), which broke a ⇧⌘X chord onto two lines.
    body->ensurePolished();
    const QFont font = body->font();
    const QVariant plain = owner ? owner->property(PLAIN_TIP_PROPERTY) : QVariant();
    const QString rich = plain.isValid()                    ? enrichedToolTip(plain.toString(), &font)
                         : text.trimmed().startsWith('<') ? text
                                                           : enrichedToolTip(text, &font);
    if (rich.isEmpty()) { hideTip(); return; }
    const QString wrapped = softBreakLongRuns(rich);
    const bool isSliding = support::isSlideMotionOk();
    const int flightMs = isSliding ? support::tipSlideInMs() : support::tipGrowMs();   // the pour's or the grow's
    bool wasDusted = false;
    bool wasSlid = false;
    // Qt re-sends ToolTip while the pointer wanders inside one control; only an
    // appearance (first show, new owner, new content) may re-shake.
    const bool isAppearing = !isVisible() || closing || owner != this->owner || wrapped != body->text();
    // A sweep hands over: the old tip leaves into its control, the new one enters from its own.
    const bool isSwitching = isVisible() && !closing && this->owner && owner != this->owner;
    if (isSwitching && !support::motionReduced() && !dust(false) && hasNonDustMotion()) support::dismissTip(*this, this->owner);
    const bool isEntering = isAppearing && (owner != this->owner || (!isRepointable() && !isWarm()));
    settleShake();
    this->owner = owner;
    body->setTip(wrapped);
    setMinimumSize(0, 0);   // sized to this text alone: no earlier tip's flight may leave its size as a floor
    adjustSize();
    if (isAppearing || placeHold.hasExpired()) place(globalPos);   // Qt's repeat ToolTip must not move a tip mid-flight
    closing = false;
    fade->stop();
    if (support::motionReduced()) {
      setWindowOpacity(1.0);
      show();
      raise();
    } else {
      const qreal from = isAppearing && !isEntering ? 1.0 : isVisible() ? windowOpacity() : 0.0;
      setWindowOpacity(from);
      show();
      raise();
      if (isAppearing && !isSwitching) {
        support::dropPopupGhosts(*this);
        if (!isEntering && dustFx) { dustFx->deleteLater(); dustFx.clear(); }
      }
      wasDusted = isEntering && dust(true);
      // No particles: it pours out of (Sliding) or grows from (Transition) its control, which then owns the opacity.
      wasSlid = !wasDusted && isEntering && owner && hasNonDustMotion() && support::revealTip(*this, owner);
      if (wasDusted) {
        setWindowOpacity(0.0);
        holdFadeKeys(fade, DUST_IN_MS);
        // No cursor tracking mid-flight, any flight: it was aimed where the tip was placed (browser
        // controlTooltip.js). A pour starts a tick late, so it is held 60ms past its landing.
        placeHold.setRemainingTime(support::motionMs(DUST_IN_MS));
      } else if (wasSlid) {
        placeHold.setRemainingTime(support::motionMs(flightMs + (isSliding ? 60 : 0)));
      } else {
        fade->setKeyValues({});
        fade->setDuration(support::motionMs(FADE_MS));
        fade->setStartValue(from);
        fade->setEndValue(1.0);
      }
      if (!wasSlid) fade->start();
    }
    // The keycap nudge waits for the gather, the pour or the grow: a shake on a tip not yet seen is lost.
    if (isEntering && hasKeycaps(rich)) {
      if (wasDusted) getShakeDelay()->start(support::motionMs(DUST_IN_MS));
      else if (wasSlid) getShakeDelay()->start(support::motionMs(flightMs));
      else shakeKeys();
    }
  }

  // No re-measure, no entrance: showFor would re-run its bookkeeping on every mouse move.
  void AppTooltip::moveTo(const QPoint& globalPos) {
    if (isVisible() && !closing && placeHold.hasExpired()) place(globalPos);
  }

  void AppTooltip::hideTip() {
    if (!isVisible()) { owner.clear(); return; }
    settleShake();
    fade->stop();
    if (support::motionReduced()) { owner.clear(); closing = false; QFrame::hide(); return; }
    // Dusted while the owner is still known.
    const bool wasDusted = dust(false);
    // Its picture leaves on the ghost dismissPopup makes.
    if (!wasDusted && owner && hasNonDustMotion() && support::dismissTip(*this, owner)) { dropTip(); return; }
    owner.clear();
    closing = true;
    fade->setKeyValues({});
    fade->setDuration(support::motionMs(wasDusted ? DUST_HAND_OVER_MS : FADE_MS));
    fade->setStartValue(windowOpacity());
    fade->setEndValue(0.0);
    fade->start();
  }

  void AppTooltip::dropTip() {
    settleShake();
    fade->stop();
    delete dustFx.data();
    owner.clear();
    closing = false;
    QFrame::hide();
  }

  // One damped left-right pass over the KEYCAPS, never a loop.
  void AppTooltip::shakeKeys() {
    if (shakeDelay) shakeDelay->stop();
    if (!isVisible() || support::motionReduced() || !support::skinTraits().hasKeycapShake) return;
    if (body->capCount() == 0) return;
    if (!shake) {
      shake = new QVariantAnimation(this);
      shake->setDuration(SHAKE_MS);
      shake->setStartValue(0.0);
      shake->setEndValue(1.0);
      QObject::connect(shake, &QVariantAnimation::valueChanged, this,
                       [this](const QVariant& v) { body->setShake(v.toDouble()); });
      QObject::connect(shake, &QVariantAnimation::finished, this,
                       [this] { body->settle(); });
    }
    shake->stop();
    body->settle();
    shake->setDuration(support::motionMs(SHAKE_MS));
    shake->start();
  }

  // false (no owner, or a box too small to grain) = the plain fade stands in.
  bool AppTooltip::dust(bool shouldGather) {
    QWidget* owner = this->owner.data();
    if (!owner || !owner->isVisible()) return false;
    // paintNow on a close: a deferred first frame was the gap the tip blinked out in.
    dustFx = flyTipDust(this, owner->window(),
                      origin.isValid() ? origin.center()
                                        : owner->mapToGlobal(owner->rect().center()), shouldGather,
                      shouldGather ? DUST_IN_MS : DUST_OUT_MS,
                      /*escapeHost=*/true, /*paintNow=*/!shouldGather);
    return dustFx != nullptr;
  }

  void AppTooltip::place(const QPoint& cursor) {
    const QScreen* scr = QGuiApplication::screenAt(cursor);
    if (!scr) scr = QGuiApplication::primaryScreen();
    const QRect avail = scr ? scr->availableGeometry() : QRect(0, 0, 1024, 768);
    int left = cursor.x() + GAP;
    int top = cursor.y() + GAP;
    if (left + width() > avail.right()) left = cursor.x() - width() - GAP;
    if (top + height() > avail.bottom()) top = cursor.y() - height() - GAP;
    left = qBound(avail.left() + 10, left, qMax(avail.left() + 10, avail.right() - width()));
    top = qBound(avail.top() + 10, top, qMax(avail.top() + 10, avail.bottom() - height()));
    move(left, top);
  }

  void AppTooltip::settleShake() {
    if (shakeDelay) shakeDelay->stop();
    if (shake) shake->stop();
    body->settle();
  }

  QTimer* AppTooltip::getShakeDelay() {
    if (!shakeDelay) {
      shakeDelay = new QTimer(this);
      shakeDelay->setSingleShot(true);
      QObject::connect(shakeDelay, &QTimer::timeout, this, [this] { shakeKeys(); });
    }
    return shakeDelay;
  }
}  // namespace stencil::gui
