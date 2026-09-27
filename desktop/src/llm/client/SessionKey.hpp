#pragma once
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>

// The anthropic session key (llm-providers.md §5): the user's own key in this process's memory only
// — never the settings JSON, QSettings or the connection store — until the TTL passes, it is
// forgotten, or Stencil quits. Browser twin: js/llm/sessionKey.js.
namespace stencil::llm {

  class SessionKey : public QObject {
    Q_OBJECT
   public:
    // The process-wide holder every window and dialog reads, wiped when the application quits.
    static SessionKey& instance();

    // `ttlMs` 0 = providers.json sessionKey.ttlMinutes; `clock` is the wall clock (a test's fake one).
    explicit SessionKey(qint64 ttlMs = 0, std::function<QDateTime()> clock = {},
                        QObject* parent = nullptr);
    ~SessionKey() override;

    // Holds `entered` for the TTL from now and returns its expiry; an empty one forgets (invalid).
    QDateTime hold(const QString& entered);
    // The held key or "": an expired one is dropped here first, so no request can carry it.
    QString key();
    // Invalid when no key is held.
    QDateTime expiresAt();
    void forget();

   signals:
    // Held, forgotten or expired.
    void changed();

   private:
    void arm();

    qint64 ttlMs;
    std::function<QDateTime()> clock;
    QString value;
    QDateTime expiry;
    QTimer expireTimer;
  };

}  // namespace stencil::llm
