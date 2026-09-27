// The anthropic wire (llm-providers.md §6.5): straight to Anthropic with the session key, the
// server's upstream body, and a failure said by the server's own reasons (upstream.go). Browser
// twin: js/llm/client.js WIRES.anthropic over js/llm/http.js.
#include "LlmClient.hpp"
#include "llmClientShared.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <initializer_list>

namespace stencil::llm {

  namespace {
    bool hasAny(const QString& s, std::initializer_list<const char*> subs) {
      for (const char* sub : subs)
        if (s.contains(QLatin1String(sub))) return true;
      return false;
    }

    // upstream.go classifyUpstream, in its order: billing first, since a spent balance arrives as a
    // 400 or a 429 depending on vendor. The whole message when recognised, else "".
    QString upstreamReason(int status, const QString& errType, const QString& message) {
      const QString t = errType.toLower();
      const QString m = message.toLower();
      if (hasAny(t, {"insufficient_quota", "billing", "credit"}) || status == 402 ||
          hasAny(m, {"credit balance", "insufficient_quota", "insufficient quota", "purchase credits",
                     "billing", "out of credits"}))
        return QStringLiteral("the LLM provider is out of credits or has no active billing");
      if (hasAny(t, {"authentication", "invalid_api_key", "permission", "unauthorized", "forbidden"}) ||
          status == 401 || status == 403)
        return QStringLiteral("the LLM provider rejected the API key");
      if (hasAny(t, {"model_not_found", "not_found"}) || status == 404 ||
          hasAny(m, {"model not found", "unknown model", "does not exist", "try pulling", "no such model"}))
        return QStringLiteral("the LLM provider does not have the requested model");
      if (t.contains(QLatin1String("rate_limit")) || status == 429)
        return QStringLiteral("the LLM provider is rate-limiting this key");
      if (status == 408 || status == 504) return QStringLiteral("the LLM provider did not respond in time");
      if (hasAny(t, {"overloaded", "api_error"}) || status >= 500)
        return QStringLiteral("the LLM provider is temporarily unavailable");
      return QString();
    }

    // sanitize.go containsSecretFragment: any 8-character run of the key is still a leak.
    bool containsSecretFragment(const QString& text, const QString& secret) {
      for (int i = 0; i + 8 <= secret.size(); ++i)
        if (text.contains(secret.mid(i, 8))) return true;
      return false;
    }

    // The Anthropic envelope {type:"error", error:{type, message}}; without a message neither field
    // counts. Unrecognised: the status and the sanitized words, dropped whole when they echo the key.
    QString upstreamErrorText(int status, const QByteArray& body, const QString& key) {
      const QJsonObject e = QJsonDocument::fromJson(body).object().value("error").toObject();
      const QString message = e.value("message").toString();
      const QString reason =
          upstreamReason(status, message.isEmpty() ? QString() : e.value("type").toString(), message);
      if (!reason.isEmpty()) return reason;
      const QString head = QStringLiteral("the LLM provider returned an error (HTTP %1)").arg(status);
      const QString detail = sanitizeProviderText(message);
      return detail.isEmpty() || containsSecretFragment(detail, key) ? head : head + QStringLiteral(": ") + detail;
    }

    // A turn's text block first (none when empty), then its images in order.
    QJsonObject anthropicMessage(const ChatMessage& m) {
      QJsonArray content;
      if (!m.text.isEmpty()) content.append(QJsonObject{{"type", "text"}, {"text", m.text}});
      for (const ChatImage& img : m.images)
        content.append(QJsonObject{
            {"type", "image"},
            {"source", QJsonObject{{"type", "base64"},
                                   {"media_type", img.mediaType},
                                   {"data", QString::fromLatin1(img.data)}}}});
      return QJsonObject{{"role", m.role}, {"content", content}};
    }
  }  // namespace

  void LlmClient::chatAnthropic(const LlmSettings& cfg, const QVector<ChatMessage>& messages,
                                const QString& system, std::function<void(LlmReply)> done) {
    const QString base = trimSlash(cfg.baseUrl);
    if (base.isEmpty()) {
      done(failReply(LlmFailure::HTTP, QStringLiteral("No Anthropic base URL configured")));
      return;
    }
    // Never entered, expired or forgotten: nothing is sent, and the UI asks for the key again.
    if (cfg.apiKey.isEmpty()) {
      done(failReply(LlmFailure::DISABLED, QString::fromUtf8(NO_SESSION_KEY_TEXT)));
      return;
    }
    if (const QString refusal = plainHttpRefusal(base); !refusal.isEmpty()) {
      done(failReply(LlmFailure::DISABLED, refusal));
      return;
    }
    const QJsonObject defaults = providersCanon().value(QStringLiteral("serverDefaults")).toObject();
    QJsonArray msgs;
    for (const ChatMessage& m : messages) msgs.append(anthropicMessage(m));
    QJsonObject body{
        {"model", cfg.model.isEmpty() ? defaults.value("model").toString() : cfg.model},
        {"max_tokens", defaults.value("maxTokens").toInt()},
        {"messages", msgs}};
    if (!system.isEmpty()) body.insert("system", system);
    transport->postJson(
        QUrl(base + chatPathOf(cfg.provider)), anthropicHeaders(cfg.apiKey),
        QJsonDocument(body).toJson(QJsonDocument::Compact),
        [endpoint = endpointTag(cfg.provider, base), key = cfg.apiKey,
         done = std::move(done)](int status, QByteArray resp, QString err) mutable {
          if (status == 0) {
            done(failReply(LlmFailure::TRANSPORT,
                           QStringLiteral("Couldn't reach %1 (%2)")
                               .arg(endpoint, err.isEmpty() ? QStringLiteral("network error") : err)));
            return;
          }
          if (!net::isOkStatus(status)) {
            done(failReply(LlmFailure::HTTP, QStringLiteral("%1: %2").arg(
                                                 endpoint, upstreamErrorText(status, resp, key))));
            return;
          }
          const QJsonObject o = QJsonDocument::fromJson(resp).object();
          LlmReply r;
          for (const QJsonValue& b : o.value("content").toArray()) {
            const QJsonObject block = b.toObject();
            if (block.value("type").toString() == QLatin1String("text"))
              r.text += block.value("text").toString();
          }
          r.model = o.value("model").toString();
          r.stopReason = o.value("stop_reason").toString();
          if (settledByStopReason(r, done)) return;
          if (!o.value("content").isArray()) {
            done(failReply(LlmFailure::BAD_RESPONSE, QStringLiteral("malformed response (no content[] text)")));
            return;
          }
          r.ok = true;
          done(r);
        });
  }
}  // namespace stencil::llm
