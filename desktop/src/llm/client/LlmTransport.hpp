#pragma once
#include <QByteArray>
#include <QList>
#include <QPair>
#include <QUrl>
#include <QString>
#include <functional>

// Seam between LlmClient and the network (llm-contract.md §6: every wire
// mapping is one JSON POST). The production implementation is qtLlmTransport
// (QNetworkAccessManager); tests substitute a mock capturing url/headers/body
// and answering with canned responses — the desktop analogue of the browser
// client's injected fetchImpl.
namespace stencil::llm {

  struct LlmTransport {
    // POST `body` (application/json) to `url` with the extra raw `headers`. `cb(status, body, error)`:
    // status is the HTTP code (0 on a transport failure, with `error` describing it).
    virtual void postJson(const QUrl& url,
                          const QList<QPair<QByteArray, QByteArray>>& headers,
                          const QByteArray& body,
                          std::function<void(int status, QByteArray body, QString error)> cb) = 0;
    // Abort the in-flight chat POST, if any: its callback then completes with a canceled transport
    // error (status 0). Default no-op for mocks that answer synchronously.
    virtual void abortActive() {}
    // GET `url` — the cheap reachability probes (ollama /api/version,
    // openai-compat /models, stencil-server /llm/info). Same callback contract.
    virtual void getJson(const QUrl& url,
                         const QList<QPair<QByteArray, QByteArray>>& headers,
                         std::function<void(int status, QByteArray body, QString error)> cb) = 0;
    virtual ~LlmTransport() = default;
  };

}  // namespace stencil::llm
