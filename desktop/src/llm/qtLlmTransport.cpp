#include "qtLlmTransport.hpp"

#include "llmSettings.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace stencil::llm {

  namespace {
    // Reachability probes must resolve fast (they drive the settings status dot).
    constexpr int kProbeTimeoutMs = 8000;

    // Qt's default policy follows redirects while re-applying raw headers, which would hand
    // the API key to a second host. Refuse: a 30x surfaces as its own status instead.
    void applyNoRedirect(QNetworkRequest& req) {
      req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QVariant::fromValue(QNetworkRequest::ManualRedirectPolicy));
    }
  }

  QtLlmTransport::QtLlmTransport(QObject* parent)
      : QObject(parent), nam_(new QNetworkAccessManager(this)) {}

  QtLlmTransport::~QtLlmTransport() = default;

  void QtLlmTransport::dispatch(
      QNetworkReply* reply,
      std::function<void(int status, QByteArray body, QString error)> cb) {
    // Context object is nam_ (owned by this transport): if the transport dies the
    // connection is severed and the lambda never runs on a dangling reply — the
    // same lifetime pattern as ServerClient::requestAsync.
    QObject::connect(reply, &QNetworkReply::finished, nam_,
                     [reply, cb = std::move(cb)]() {
                       const int status =
                           reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                       const QByteArray data = reply->readAll();
                       QString err;
                       if (reply->error() != QNetworkReply::NoError && status == 0)
                         err = reply->errorString();
                       reply->deleteLater();
                       cb(status, data, err);
                     });
  }

  void QtLlmTransport::postJson(
      const QUrl& url, const QList<QPair<QByteArray, QByteArray>>& headers,
      const QByteArray& body,
      std::function<void(int status, QByteArray body, QString error)> cb) {
    QNetworkRequest req(url);
    // LLM chats are slow (a local vision model can chew for minutes): the canon
    // timeouts.chatSeconds (120 s), not serverClient's 20 s REST timeout.
    req.setTransferTimeout(llmChatTimeoutMs());
    applyNoRedirect(req);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    for (const auto& h : headers) req.setRawHeader(h.first, h.second);
    QNetworkReply* reply = nam_->post(req, body);
    activePost_ = reply;  // abortActive() targets the latest chat POST
    dispatch(reply, std::move(cb));
  }

  void QtLlmTransport::abortActive() {
    if (activePost_) activePost_->abort();  // finished() fires with OperationCanceled
  }

  void QtLlmTransport::getJson(
      const QUrl& url, const QList<QPair<QByteArray, QByteArray>>& headers,
      std::function<void(int status, QByteArray body, QString error)> cb) {
    QNetworkRequest req(url);
    req.setTransferTimeout(kProbeTimeoutMs);
    applyNoRedirect(req);
    for (const auto& h : headers) req.setRawHeader(h.first, h.second);
    dispatch(nam_->get(req), std::move(cb));
  }

}  // namespace stencil::llm
