#include "AppTooltip.hpp"

namespace stencil::gui {

  AppTooltip::AppTooltip(QWidget* parent) : QFrame(parent, Qt::ToolTip) {
    setObjectName(QString::fromLatin1(OBJECT_NAME));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    body_ = new TipBody(this);
    body_->setTextFormat(Qt::RichText);
    body_->setObjectName(QStringLiteral("stencilAppTooltipBody"));
    // Browser #app-tooltip max-width: 380px.
    body_->setWordWrap(true);
    body_->setMaximumWidth(MAX_TIP_WIDTH);
    lay->addWidget(body_);
    hide();

    fade_ = new QVariantAnimation(this);
    fade_->setDuration(FADE_MS);
    QObject::connect(fade_, &QVariantAnimation::valueChanged, this,
                     [this](const QVariant& v) { setWindowOpacity(v.toDouble()); });
    QObject::connect(fade_, &QVariantAnimation::finished, this, [this] {
      if (closing_) { closing_ = false; QFrame::hide(); }
    });
    // Anti-stranding heartbeat: a fast sweep can leave the owner without a Leave we see.
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


  // Qt breaks a rich-text line at spaces only, so a URL or a long file name runs past
  // MAX_TIP_WIDTH and the body clips it. Zero-width spaces give the layout somewhere to
  // break; the browser gets the same from `overflow-wrap: anywhere` (components/tooltip.css).
  static QString softBreakLongRuns(const QString& rich) {
    constexpr int RUN = 18;   // characters one unbroken token may hold before an opportunity
    QString out;
    out.reserve(rich.size() + rich.size() / RUN);
    int run = 0;
    for (int i = 0; i < rich.size(); ++i) {
      const QChar c = rich.at(i);
      if (c == u'<') {                       // markup, not text: copied whole
        const int end = rich.indexOf(u'>', i);
        out += rich.mid(i, (end < 0 ? rich.size() : end + 1) - i);
        i = (end < 0 ? rich.size() : end);
        run = 0;
        continue;
      }
      if (c == u'&') {                       // an entity is one character, never split
        const int end = rich.indexOf(u';', i);
        if (end > i && end - i <= 8) { out += rich.mid(i, end + 1 - i); i = end; ++run; continue; }
      }
      out += c;
      if (c.isSpace()) run = 0;
      else if (++run >= RUN) { out += QChar(0x200B); run = 0; }
    }
    return out;
  }

  // `originGlobal` invalid = the owner's centre (wrong for an item view's viewport).
  void AppTooltip::showFor(QWidget* owner, const QString& text, const QPoint& globalPos,
                           const QRect& originGlobal) {
    origin_ = originGlobal;
    // Re-rendered from the plain text in THIS body's font: the owner's copy was measured
    // in QToolTip's (11pt on macOS vs 13pt), which broke a ⇧⌘X chord onto two lines.
    body_->ensurePolished();
    const QFont font = body_->font();
    const QVariant plain = owner ? owner->property(PLAIN_TIP_PROPERTY) : QVariant();
    const QString rich = plain.isValid()                    ? enrichedToolTip(plain.toString(), &font)
                         : text.trimmed().startsWith('<') ? text
                                                           : enrichedToolTip(text, &font);
    if (rich.isEmpty()) { hideTip(); return; }
    const QString wrapped = softBreakLongRuns(rich);
    bool dusted = false;
    // Qt re-sends ToolTip while the pointer wanders inside one control; only an
    // appearance (first show, new owner, new content) may re-shake.
    const bool appearing = !isVisible() || closing_ || owner != owner_ || wrapped != body_->text();
    settleShake();
    owner_ = owner;
    body_->setTip(wrapped);
    adjustSize();
    place(globalPos);
    closing_ = false;
    fade_->stop();
    if (support::motionReduced()) {
      setWindowOpacity(1.0);
      show();
      raise();
    } else {
      const qreal from = isVisible() ? windowOpacity() : 0.0;
      setWindowOpacity(from);
      show();
      raise();
      dusted = appearing && dust(true);
      if (dusted) {
        setWindowOpacity(0.0);
        holdFadeKeys(fade_, DUST_IN_MS);
        // No cursor tracking mid-flight: the cloud was aimed where the tip was placed
        // (browser controlTooltip.js).
        placeHold_.setRemainingTime(DUST_IN_MS);
      } else {
        fade_->setKeyValues({});
        fade_->setDuration(FADE_MS);
        fade_->setStartValue(from);
        fade_->setEndValue(1.0);
      }
      fade_->start();
    }
    // The keycap nudge waits for the gather — a shake behind the motes cannot be seen.
    if (appearing && hasKeycaps(rich)) {
      if (dusted) shakeDelay()->start(DUST_IN_MS);
      else shakeKeys();
    }
  }


  // No re-measure, no entrance: showFor would re-run its bookkeeping on every mouse move.
  void AppTooltip::moveTo(const QPoint& globalPos) {
    if (isVisible() && !closing_ && placeHold_.hasExpired()) place(globalPos);
  }

  void AppTooltip::hideTip() {
    if (!isVisible()) { owner_.clear(); return; }
    settleShake();
    fade_->stop();
    if (support::motionReduced()) { owner_.clear(); closing_ = false; QFrame::hide(); return; }
    // Dusted while the owner is still known.
    const bool dusted = dust(false);
    owner_.clear();
    closing_ = true;
    fade_->setKeyValues({});
    fade_->setDuration(dusted ? DUST_HAND_OVER_MS : FADE_MS);
    fade_->setStartValue(windowOpacity());
    fade_->setEndValue(0.0);
    fade_->start();
  }


  // One damped left-right pass over the KEYCAPS, never a loop.
  void AppTooltip::shakeKeys() {
    if (shakeDelay_) shakeDelay_->stop();
    if (!isVisible() || support::motionReduced()) return;
    if (body_->capCount() == 0) return;
    if (!shake_) {
      shake_ = new QVariantAnimation(this);
      shake_->setDuration(SHAKE_MS);
      shake_->setStartValue(0.0);
      shake_->setEndValue(1.0);
      QObject::connect(shake_, &QVariantAnimation::valueChanged, this,
                       [this](const QVariant& v) { body_->setShake(v.toDouble()); });
      QObject::connect(shake_, &QVariantAnimation::finished, this,
                       [this] { body_->settle(); });
    }
    shake_->stop();
    body_->settle();
    shake_->start();
  }


  // false (no owner, or a box too small to grain) = the plain fade stands in.
  bool AppTooltip::dust(bool gather) {
    QWidget* owner = owner_.data();
    if (!owner || !owner->isVisible()) return false;
    // paintNow on a close: a deferred first frame was the gap the tip blinked out in.
    return flyTipDust(this, owner->window(),
                      origin_.isValid() ? origin_.center()
                                        : owner->mapToGlobal(owner->rect().center()), gather,
                      gather ? DUST_IN_MS : DUST_OUT_MS,
                      /*escapeHost=*/true, /*paintNow=*/!gather)
           != nullptr;
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
    if (shakeDelay_) shakeDelay_->stop();
    if (shake_) shake_->stop();
    body_->settle();
  }


  QTimer* AppTooltip::shakeDelay() {
    if (!shakeDelay_) {
      shakeDelay_ = new QTimer(this);
      shakeDelay_->setSingleShot(true);
      QObject::connect(shakeDelay_, &QTimer::timeout, this, [this] { shakeKeys(); });
    }
    return shakeDelay_;
  }
}  // namespace stencil::gui
