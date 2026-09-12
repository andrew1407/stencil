#pragma once
// The provider wire helpers — endpoint tags, failure replies and text sanitising — private to the llmClient*.cpp TUs.
#include "llmClient.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QString>

namespace stencil::llm {


  inline QString trimSlash(QString url) {
    while (url.endsWith(QLatin1Char('/'))) url.chop(1);
    return url;
  }

  inline LlmReply failReply(LlmFailure kind, const QString& msg) {
    LlmReply r;
    r.failure = kind;
    r.error = msg;
    return r;
  }

  // Browser chatSession.unreachableText parity: every surface speaks the same
  // error voice — "<provider label> at <host>", host with the scheme dropped.
  inline QString endpointTag(const QString& provider, const QString& url) {
    QString label = llmProviderDisplayName(provider);  // providers.json canon
    if (label.isEmpty()) label = provider;
    QString host = url;
    if (host.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) host = host.mid(8);
    else if (host.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)) host = host.mid(7);
    return host.isEmpty() ? label : label + QStringLiteral(" at ") + host;
  }

  // How much of a provider's own prose an error may quote (server upstream.go parity).
  inline constexpr int kMaxProviderDetail = 200;

  // Provider prose is untrusted: control characters out, URLs and token-shaped runs
  // redacted, whitespace collapsed, hard-truncated. Port of the browser client's
  // sanitizeProviderText — an error body never reaches the chat as itself.
  inline QString sanitizeProviderText(QString text) {
    text.truncate(4 * kMaxProviderDetail);
    for (QChar& c : text)
      if (!c.isPrint()) c = QLatin1Char(' ');
    static const QRegularExpression url(QStringLiteral(R"([a-zA-Z][a-zA-Z0-9+.-]*://\S+)"));
    static const QRegularExpression secret(
        QStringLiteral(R"((?:bearer|basic) +[A-Za-z0-9._~+/=-]{8,})"
                       R"(|\b(?:sk|pk|api[-_]?key|key|token|secret)[-_=:][A-Za-z0-9._-]{6,})"
                       R"(|[A-Za-z0-9_-]{24,})"),
        QRegularExpression::CaseInsensitiveOption);
    text.replace(url, QStringLiteral("[redacted]"));
    text.replace(secret, QStringLiteral("[redacted]"));
    text = text.simplified();
    if (text.size() > kMaxProviderDetail)
      text = text.left(kMaxProviderDetail - 1).trimmed() + QStringLiteral("…");
    return text;
  }

  // Shared HTTP outcome triage: transport error / non-2xx (with the server's
  // JSON "message"/"error" surfaced when present), phrased like the browser's
  // unreachableText. Returns true when the caller should stop (done invoked).
  // `serverHost` non-empty ⇒ this is the stencil-server provider, whose 401/403
  // means OUR SESSION expired (the local providers have no session to expire).
  inline bool httpFailed(const QString& endpoint, int status, const QByteArray& body,
                  const QString& err, std::function<void(LlmReply)>& done,
                  const QString& serverHost = QString()) {
    if (status == 0) {
      done(failReply(LlmFailure::Transport,
                     QStringLiteral("Couldn't reach %1 (%2)")
                         .arg(endpoint,
                              err.isEmpty() ? QStringLiteral("network error") : err)));
      return true;
    }
    if (!serverHost.isEmpty() && (status == 401 || status == 403)) {
      LlmReply r = failReply(LlmFailure::Expired,
                             QStringLiteral("Your session on %1 has expired — reconnect to "
                                            "that server, then send this again.")
                                 .arg(serverHost));
      r.expiredHost = serverHost;   // the chat's "Reconnect to <host>" CTA target
      done(r);
      return true;
    }
    if (status < 200 || status >= 300) {
      const QJsonObject o = QJsonDocument::fromJson(body).object();
      const QString detail = o.value("message").toString(
          o.value("error").toObject().value("message").toString(
              o.value("error").toString()));
      const QString clean = sanitizeProviderText(detail);
      // The reason is said ONCE (contract §6.3): the endpoint labels the provider's
      // own message, and nothing restates it — no status, no second sentence.
      const QString why = clean.isEmpty() ? QStringLiteral("HTTP %1").arg(status) : clean;
      // The server's 503 llmDisabled is a typed Disabled (browser kind parity) —
      // a configure hint, not a broken transport. Its message IS the sentence
      // (browser describeChatError's "notice" text is the raw err.message,
      // never run through the "<provider> at <host>:" wrapper below).
      const bool disabled = o.value("code").toString() == QLatin1String("llmDisabled");
      done(failReply(disabled ? LlmFailure::Disabled : LlmFailure::Http,
                     disabled ? why : QStringLiteral("%1: %2").arg(endpoint, why)));
      return true;
    }
    return false;
  }

  // Shared handler for the ollama / openai-compat chat POSTs: HTTP triage,
  // then `pick` extracts the reply text (BadResponse when it isn't a string).
  inline std::function<void(int, QByteArray, QString)> textReplyHandler(
      QString endpoint, std::function<void(LlmReply)> done,
      QJsonValue (*pick)(const QJsonObject&), const char* malformedMsg) {
    return [endpoint = std::move(endpoint), done = std::move(done), pick,
            malformedMsg](int status, QByteArray resp, QString err) mutable {
      if (httpFailed(endpoint, status, resp, err, done)) return;
      const QJsonValue content = pick(QJsonDocument::fromJson(resp).object());
      if (!content.isString()) {
        done(failReply(LlmFailure::BadResponse, QString::fromUtf8(malformedMsg)));
        return;
      }
      LlmReply r;
      r.ok = true;
      r.text = content.toString();
      done(r);
    };
  }

  // Shared GET-probe triage: transport / non-2xx failures fill `detail`; a
  // 2xx body is handed to `fill` to interpret.
  inline std::function<void(int, QByteArray, QString)> probeHandler(
      std::function<void(LlmProbeResult)> done,
      std::function<void(const QJsonObject&, LlmProbeResult&)> fill) {
    return [done = std::move(done), fill = std::move(fill)](int status, QByteArray body,
                                                            QString err) mutable {
      LlmProbeResult r;
      if (status == 0) {
        r.detail = err.isEmpty() ? QStringLiteral("network error") : err;
      } else if (status < 200 || status >= 300) {
        r.detail = QStringLiteral("HTTP %1").arg(status);
      } else {
        fill(QJsonDocument::fromJson(body).object(), r);
      }
      done(r);
    };
  }


}  // namespace stencil::llm
