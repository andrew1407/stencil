#include "LlmClient.hpp"
#include "llmClientShared.hpp"

#include "connectionStore.hpp"
#include "opRegistry.hpp"
#include "ServerClient.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace stencil::llm {

  // ollama — native chat (contract §6.1)

  void LlmClient::chatOllama(const LlmSettings& cfg, const QVector<ChatMessage>& messages,
                             const QString& system, std::function<void(LlmReply)> done) {
    QJsonArray msgs;
    msgs.append(QJsonObject{{"role", "system"}, {"content", system}});
    for (const ChatMessage& m : messages) {
      QJsonObject o{{"role", m.role}, {"content", m.text}};
      if (!m.images.isEmpty()) {
        QJsonArray imgs;
        for (const ChatImage& img : m.images) imgs.append(QString::fromLatin1(img.data));
        o.insert("images", imgs);
      }
      msgs.append(o);
    }
    const QJsonObject body{{"model", cfg.model}, {"stream", false}, {"messages", msgs}};
    transport_->postJson(
        QUrl(trimSlash(cfg.baseUrl) + QStringLiteral("/api/chat")), {},
        QJsonDocument(body).toJson(QJsonDocument::Compact),
        textReplyHandler(
            endpointTag(cfg.provider, trimSlash(cfg.baseUrl)), std::move(done),
            [](const QJsonObject& o) { return o.value("message").toObject().value("content"); },
            "malformed ollama response (no message.content)"));
  }

  // openai-compat — LM Studio & friends (contract §6.2)

  void LlmClient::chatOpenAi(const LlmSettings& cfg, const QVector<ChatMessage>& messages,
                             const QString& system, std::function<void(LlmReply)> done) {
    QJsonArray msgs;
    msgs.append(QJsonObject{{"role", "system"}, {"content", system}});
    for (const ChatMessage& m : messages) {
      QJsonObject o{{"role", m.role}};
      if (m.images.isEmpty()) {
        o.insert("content", m.text);
      } else {
        QJsonArray parts;
        parts.append(QJsonObject{{"type", "text"}, {"text", m.text}});
        for (const ChatImage& img : m.images) {
          const QString url = QStringLiteral("data:%1;base64,%2")
                                  .arg(img.mediaType, QString::fromLatin1(img.data));
          parts.append(QJsonObject{{"type", "image_url"},
                                   {"image_url", QJsonObject{{"url", url}}}});
        }
        o.insert("content", parts);
      }
      msgs.append(o);
    }
    const QJsonObject body{{"model", cfg.model}, {"stream", false}, {"messages", msgs}};
    QList<QPair<QByteArray, QByteArray>> headers;
    if (!cfg.apiKey.isEmpty())
      headers.append({QByteArrayLiteral("Authorization"),
                      QByteArrayLiteral("Bearer ") + cfg.apiKey.toUtf8()});
    transport_->postJson(
        QUrl(trimSlash(cfg.baseUrl) + QStringLiteral("/chat/completions")), headers,
        QJsonDocument(body).toJson(QJsonDocument::Compact),
        textReplyHandler(
            endpointTag(cfg.provider, trimSlash(cfg.baseUrl)), std::move(done),
            [](const QJsonObject& o) {
              return o.value("choices").toArray().at(0).toObject()
                  .value("message").toObject().value("content");
            },
            "malformed response (no choices[0].message.content)"));
  }

  // stencil-server — Anthropic proxy (contract §6.3)

  void LlmClient::chatServer(const LlmSettings& cfg, const QVector<ChatMessage>& messages,
                             const QString& system, std::function<void(LlmReply)> done) {
    const QString base = net::ServerClient::normalizeBase(cfg.serverUrl);
    if (base.isEmpty()) {
      done(failReply(LlmFailure::HTTP, QStringLiteral("no collaboration server configured")));
      return;
    }
    const QString token = tokenResolver_(cfg.serverUrl);
    if (token.isEmpty()) {
      done(failReply(LlmFailure::HTTP,
                     QStringLiteral("not connected to %1 (no token)").arg(base)));
      return;
    }
    // protocol.LlmChatRequest, re-declared here (the same mirror rule as the
    // rest of the protocol package).
    QJsonArray msgs;
    for (const ChatMessage& m : messages) {
      QJsonObject o{{"role", m.role}, {"text", m.text}};
      if (!m.images.isEmpty()) {
        QJsonArray imgs;
        for (const ChatImage& img : m.images)
          imgs.append(QJsonObject{{"mediaType", img.mediaType},
                                  {"data", QString::fromLatin1(img.data)}});
        o.insert("images", imgs);
      }
      msgs.append(o);
    }
    const QJsonObject body{{"system", system}, {"messages", msgs}, {"model", cfg.model}};
    transport_->postJson(
        QUrl(base + QStringLiteral("/llm/chat")),
        {{QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + token.toUtf8()}},
        QJsonDocument(body).toJson(QJsonDocument::Compact),
        [endpoint = endpointTag(cfg.provider, base), host = QUrl(base).host().isEmpty()
                                                                  ? base
                                                                  : QUrl(base).authority(),
         done = std::move(done)](int status, QByteArray resp, QString err) mutable {
          if (httpFailed(endpoint, status, resp, err, done, host)) return;
          const QJsonObject o = QJsonDocument::fromJson(resp).object();  // LlmChatResponse
          LlmReply r;
          r.text = o.value("text").toString();
          r.model = o.value("model").toString();
          r.stopReason = o.value("stopReason").toString();
          // Truncation / refusal are typed errors — never parsed as plans.
          if (r.stopReason == QLatin1String("max_tokens")) {
            r.failure = LlmFailure::TRUNCATED;
            r.error = QStringLiteral("Response truncated — the model hit its output "
                                     "limit; try a shorter request");
            done(r);
            return;
          }
          if (r.stopReason == QLatin1String("refusal")) {
            r.failure = LlmFailure::REFUSAL;
            r.error = r.text.isEmpty() ? QStringLiteral("The model refused this request")
                                       : r.text;
            done(r);
            return;
          }
          if (r.text.isEmpty()) {
            done(failReply(LlmFailure::BAD_RESPONSE,
                           QStringLiteral("malformed server response (no text)")));
            return;
          }
          r.ok = true;
          done(r);
        });
  }
}  // namespace stencil::llm

