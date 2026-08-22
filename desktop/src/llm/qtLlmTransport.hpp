#pragma once
#include "llmTransport.hpp"
#include <QObject>
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;

// Production LlmTransport over QNetworkAccessManager, following the
// net/serverClient request pattern (async reply bound to the manager so a
// callback never runs on a destroyed transport; every request carries a
// transfer timeout so a hung provider can't wedge the app). The timeout is
// generous — local models routinely take a minute-plus — and matches the
// collaboration server's LLM_TIMEOUT_SECONDS default (contract §6.3).
namespace stencil::llm {

  class QtLlmTransport : public QObject, public LlmTransport {
    Q_OBJECT
   public:
    explicit QtLlmTransport(QObject* parent = nullptr);
    ~QtLlmTransport() override;

    void postJson(const QUrl& url,
                  const QList<QPair<QByteArray, QByteArray>>& headers,
                  const QByteArray& body,
                  std::function<void(int status, QByteArray body, QString error)> cb) override;
    // Abort the tracked in-flight chat POST (probes are untouched); the reply
    // finishes with OperationCanceled → status 0 at the callback.
    void abortActive() override;
    void getJson(const QUrl& url,
                 const QList<QPair<QByteArray, QByteArray>>& headers,
                 std::function<void(int status, QByteArray body, QString error)> cb) override;

   private:
    // Shared completion wiring for post/get: read status/body/error, delete the
    // reply, deliver `cb`.
    void dispatch(QNetworkReply* reply,
                  std::function<void(int status, QByteArray body, QString error)> cb);

    QNetworkAccessManager* nam_;
    QPointer<QNetworkReply> activePost_;  // the abortable in-flight chat POST
  };

}  // namespace stencil::llm
