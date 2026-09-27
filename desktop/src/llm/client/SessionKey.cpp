// The anthropic session key's holder (llm-providers.md §5): one expiry, one timer, and the wall
// clock read again before every use, so a machine that slept past the TTL still drops the key
// before the next request. Browser twin: js/llm/sessionKey.js.
#include "SessionKey.hpp"
#include "llmSettings.hpp"

#include <QCoreApplication>
#include <QPointer>
#include <algorithm>
#include <climits>

namespace stencil::llm {

  SessionKey& SessionKey::instance() {
    static QPointer<SessionKey> held;
    if (!held) {
      held = new SessionKey(0, {}, QCoreApplication::instance());
      if (QCoreApplication::instance())
        connect(qApp, &QCoreApplication::aboutToQuit, held.data(), &SessionKey::forget);
    }
    return *held;
  }

  SessionKey::SessionKey(qint64 ttlMs, std::function<QDateTime()> clock, QObject* parent)
      : QObject(parent),
        ttlMs(ttlMs > 0 ? ttlMs : qint64(sessionKeyTtlMinutes()) * 60 * 1000),
        clock(clock ? std::move(clock) : [] { return QDateTime::currentDateTime(); }) {
    expireTimer.setSingleShot(true);
    expireTimer.setTimerType(Qt::PreciseTimer);
    connect(&expireTimer, &QTimer::timeout, this, [this] {
      if (!value.isEmpty() && this->clock() < expiry) arm();   // fired early: wait out the rest
      else key();
    });
  }

  SessionKey::~SessionKey() { value.fill(QChar(u'\0')); }

  QDateTime SessionKey::hold(const QString& entered) {
    const QString k = entered.trimmed();
    if (k.isEmpty()) {
      forget();
      return QDateTime();
    }
    value.fill(QChar(u'\0'));
    value = k;
    expiry = clock().addMSecs(ttlMs);
    arm();
    emit changed();
    return expiry;
  }

  QString SessionKey::key() {
    if (!value.isEmpty() && clock() >= expiry) forget();
    return value;
  }

  QDateTime SessionKey::expiresAt() { return key().isEmpty() ? QDateTime() : expiry; }

  void SessionKey::forget() {
    const bool had = !value.isEmpty();
    value.fill(QChar(u'\0'));   // our own buffer; a copy a request still holds is its own
    value.clear();
    expiry = QDateTime();
    expireTimer.stop();
    if (had) emit changed();
  }

  void SessionKey::arm() {
    const qint64 left = std::max<qint64>(0, clock().msecsTo(expiry));
    expireTimer.start(int(std::min<qint64>(left, INT_MAX)));
  }

}  // namespace stencil::llm
