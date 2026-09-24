#include "AppTooltip.hpp"
#include "../skinPrefs.hpp"

namespace stencil::gui {

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


  // Qt breaks a rich-text line at spaces only, so a URL or long file name runs past MAX_TIP_WIDTH.
  // Zero-width spaces give the layout somewhere to break (browser: overflow-wrap: anywhere).
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
    bool dusted = false;
    // Qt re-sends ToolTip while the pointer wanders inside one control; only an
    // appearance (first show, new owner, new content) may re-shake.
    const bool appearing = !isVisible() || closing || owner != this->owner || wrapped != body->text();
    settleShake();
    this->owner = owner;
    body->setTip(wrapped);
    adjustSize();
    place(globalPos);
    closing = false;
    fade->stop();
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
        holdFadeKeys(fade, DUST_IN_MS);
        // No cursor tracking mid-flight: the cloud was aimed where the tip was placed
        // (browser controlTooltip.js).
        placeHold.setRemainingTime(DUST_IN_MS);
      } else {
        fade->setKeyValues({});
        fade->setDuration(FADE_MS);
        fade->setStartValue(from);
        fade->setEndValue(1.0);
      }
      fade->start();
    }
    // The keycap nudge waits for the gather — a shake behind the motes cannot be seen.
    if (appearing && hasKeycaps(rich)) {
      if (dusted) getShakeDelay()->start(DUST_IN_MS);
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
    const bool dusted = dust(false);
    owner.clear();
    closing = true;
    fade->setKeyValues({});
    fade->setDuration(dusted ? DUST_HAND_OVER_MS : FADE_MS);
    fade->setStartValue(windowOpacity());
    fade->setEndValue(0.0);
    fade->start();
  }


  // One damped left-right pass over the KEYCAPS, never a loop.
  void AppTooltip::shakeKeys() {
    if (shakeDelay) shakeDelay->stop();
    if (!isVisible() || support::motionReduced() || support::isWebcore()) return;
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
    shake->start();
  }


  // false (no owner, or a box too small to grain) = the plain fade stands in.
  bool AppTooltip::dust(bool gather) {
    QWidget* owner = this->owner.data();
    if (!owner || !owner->isVisible()) return false;
    // paintNow on a close: a deferred first frame was the gap the tip blinked out in.
    return flyTipDust(this, owner->window(),
                      origin.isValid() ? origin.center()
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
