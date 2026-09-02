#include "llmClient.hpp"

#include "connectionStore.hpp"
#include "opRegistry.hpp"
#include "serverClient.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace stencil::llm {

  namespace {

    QString trimSlash(QString url) {
      while (url.endsWith(QLatin1Char('/'))) url.chop(1);
      return url;
    }

    LlmReply failReply(LlmFailure kind, const QString& msg) {
      LlmReply r;
      r.failure = kind;
      r.error = msg;
      return r;
    }

    // Browser chatSession.unreachableText parity: every surface speaks the same
    // error voice — "<provider label> at <host>", host with the scheme dropped.
    QString endpointTag(const QString& provider, const QString& url) {
      QString label = llmProviderDisplayName(provider);  // providers.json canon
      if (label.isEmpty()) label = provider;
      QString host = url;
      if (host.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) host = host.mid(8);
      else if (host.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)) host = host.mid(7);
      return host.isEmpty() ? label : label + QStringLiteral(" at ") + host;
    }

    // How much of a provider's own prose an error may quote (server upstream.go parity).
    constexpr int kMaxProviderDetail = 200;

    // Provider prose is untrusted: control characters out, URLs and token-shaped runs
    // redacted, whitespace collapsed, hard-truncated. Port of the browser client's
    // sanitizeProviderText — an error body never reaches the chat as itself.
    QString sanitizeProviderText(QString text) {
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
    bool httpFailed(const QString& endpoint, int status, const QByteArray& body,
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
    std::function<void(int, QByteArray, QString)> textReplyHandler(
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
    std::function<void(int, QByteArray, QString)> probeHandler(
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

  }  // namespace

  QString LlmClient::savedServerToken(const QString& serverUrl) {
    const QString base = net::ServerClient::normalizeBase(serverUrl);
    const auto saved = net::connectionStore::loadSavedServers();
    for (const auto& s : saved)
      if (net::ServerClient::normalizeBase(s.url) == base) return s.token;
    return QString();
  }

  LlmClient::LlmClient(LlmTransport* transport)
      : transport_(transport), tokenResolver_(&LlmClient::savedServerToken) {}

  void LlmClient::setServerTokenResolver(
      std::function<QString(const QString& serverUrl)> resolver) {
    if (resolver) tokenResolver_ = std::move(resolver);
  }

  void LlmClient::abort() { transport_->abortActive(); }

  QString LlmClient::systemPrompt(const QString& suffix) {
    // §4 prose core + the ops section assembled from the §13 op registry
    // (the §10 editor block sits between the frame and image bullets).
    QString s = assembledSystemPrompt();
    if (!suffix.isEmpty()) s += QStringLiteral("\n\n") + suffix;
    return s;
  }

  void LlmClient::chat(const LlmSettings& cfg, const QVector<ChatMessage>& messages,
                       const QString& systemSuffix, std::function<void(LlmReply)> done) {
    // Local-only "assistant off" (contract §5 note): a typed config error,
    // never a transport call.
    if (cfg.provider == QLatin1String("none")) {
      done(failReply(LlmFailure::Off,
                     QStringLiteral("The assistant is turned off — choose a provider "
                                    "to enable it.")));
      return;
    }
    const QString system = systemPrompt(systemSuffix);
    if (cfg.provider == QLatin1String("stencil-server")) {
      chatServer(cfg, messages, system, std::move(done));
    } else if (cfg.provider == QLatin1String("openai-compat")) {
      chatOpenAi(cfg, messages, system, std::move(done));
    } else if (cfg.provider == QLatin1String("ollama")) {
      chatOllama(cfg, messages, system, std::move(done));
    } else {
      done(failReply(LlmFailure::BadResponse,
                     QStringLiteral("Unknown LLM provider \"%1\"").arg(cfg.provider)));
    }
  }

  // ── reachability probe (chat-dock status dot) ─────────────────────────────

  void LlmClient::probe(const LlmSettings& cfg, std::function<void(LlmProbeResult)> done) {
    const auto fail = [&done](const QString& detail) {
      LlmProbeResult r;
      r.detail = detail;
      done(r);
    };
    // "none" probes nothing — the assistant is off by configuration.
    if (cfg.provider == QLatin1String("none"))
      return fail(QStringLiteral("assistant turned off"));
    if (cfg.provider == QLatin1String("stencil-server")) {
      const QString base = net::ServerClient::normalizeBase(cfg.serverUrl);
      if (base.isEmpty()) return fail(QStringLiteral("no collaboration server configured"));
      const QString token = tokenResolver_(cfg.serverUrl);
      if (token.isEmpty())
        return fail(QStringLiteral("not connected (no token for %1)").arg(base));
      transport_->getJson(
          QUrl(base + QStringLiteral("/llm/info")),
          {{QByteArrayLiteral("Authorization"),
            QByteArrayLiteral("Bearer ") + token.toUtf8()}},
          probeHandler(std::move(done), [](const QJsonObject& o, LlmProbeResult& r) {
            r.model = o.value("model").toString();
            if (o.value("enabled").toBool(false)) r.ok = true;
            else r.detail = QStringLiteral("LLM disabled on the server");
          }));
      return;
    }
    const bool openai = cfg.provider == QLatin1String("openai-compat");
    QList<QPair<QByteArray, QByteArray>> headers;
    if (openai && !cfg.apiKey.isEmpty())
      headers.append({QByteArrayLiteral("Authorization"),
                      QByteArrayLiteral("Bearer ") + cfg.apiKey.toUtf8()});
    const QUrl url(trimSlash(cfg.baseUrl) +
                   (openai ? QStringLiteral("/models") : QStringLiteral("/api/version")));
    transport_->getJson(
        url, headers,
        probeHandler(std::move(done), [](const QJsonObject& o, LlmProbeResult& r) {
          r.ok = true;
          // Ollama's /api/version reports a version string worth surfacing.
          r.detail = o.value("version").toString();
        }));
  }

  // ── model suggestions (settings UI; browser listModels parity) ─────────────

  void LlmClient::listModels(const LlmSettings& cfg, std::function<void(QStringList)> done) {
    using Pick = QStringList (*)(const QJsonObject&);
    // Any non-2xx / transport failure ⇒ empty list (suggestions are optional).
    const auto handler = [](std::function<void(QStringList)> done, Pick pick) {
      return [done = std::move(done), pick](int status, QByteArray body, QString) mutable {
        QStringList out;
        if (status >= 200 && status < 300)
          out = pick(QJsonDocument::fromJson(body).object());
        done(out);
      };
    };
    if (cfg.provider == QLatin1String("none")) {  // assistant off — nothing to list
      done({});
      return;
    }
    if (cfg.provider == QLatin1String("stencil-server")) {
      const QString base = net::ServerClient::normalizeBase(cfg.serverUrl);
      const QString token = base.isEmpty() ? QString() : tokenResolver_(cfg.serverUrl);
      if (token.isEmpty()) {
        done({});
        return;
      }
      transport_->getJson(
          QUrl(base + QStringLiteral("/llm/info")),
          {{QByteArrayLiteral("Authorization"),
            QByteArrayLiteral("Bearer ") + token.toUtf8()}},
          handler(std::move(done), [](const QJsonObject& o) {
            const QString model = o.value("model").toString();
            return model.isEmpty() ? QStringList() : QStringList{model};
          }));
      return;
    }
    const bool openai = cfg.provider == QLatin1String("openai-compat");
    QList<QPair<QByteArray, QByteArray>> headers;
    if (openai && !cfg.apiKey.isEmpty())
      headers.append({QByteArrayLiteral("Authorization"),
                      QByteArrayLiteral("Bearer ") + cfg.apiKey.toUtf8()});
    const QUrl url(trimSlash(cfg.baseUrl) +
                   (openai ? QStringLiteral("/models") : QStringLiteral("/api/tags")));
    transport_->getJson(
        url, headers,
        handler(std::move(done),
                openai ? Pick([](const QJsonObject& o) {
                  QStringList out;
                  for (const QJsonValue& v : o.value("data").toArray()) {
                    const QString id = v.toObject().value("id").toString();
                    if (!id.isEmpty()) out << id;
                  }
                  return out;
                })
                       : Pick([](const QJsonObject& o) {
                  QStringList out;
                  for (const QJsonValue& v : o.value("models").toArray()) {
                    const QString name = v.toObject().value("name").toString();
                    if (!name.isEmpty()) out << name;
                  }
                  return out;
                })));
  }

  // ── ollama — native chat (contract §6.1) ───────────────────────────────────

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

  // ── openai-compat — LM Studio & friends (contract §6.2) ────────────────────

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

  // ── stencil-server — Anthropic proxy (contract §6.3) ───────────────────────

  void LlmClient::chatServer(const LlmSettings& cfg, const QVector<ChatMessage>& messages,
                             const QString& system, std::function<void(LlmReply)> done) {
    const QString base = net::ServerClient::normalizeBase(cfg.serverUrl);
    if (base.isEmpty()) {
      done(failReply(LlmFailure::Http, QStringLiteral("no collaboration server configured")));
      return;
    }
    const QString token = tokenResolver_(cfg.serverUrl);
    if (token.isEmpty()) {
      done(failReply(LlmFailure::Http,
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
            r.failure = LlmFailure::Truncated;
            r.error = QStringLiteral("Response truncated — the model hit its output "
                                     "limit; try a shorter request");
            done(r);
            return;
          }
          if (r.stopReason == QLatin1String("refusal")) {
            r.failure = LlmFailure::Refusal;
            r.error = r.text.isEmpty() ? QStringLiteral("The model refused this request")
                                       : r.text;
            done(r);
            return;
          }
          if (r.text.isEmpty()) {
            done(failReply(LlmFailure::BadResponse,
                           QStringLiteral("malformed server response (no text)")));
            return;
          }
          r.ok = true;
          done(r);
        });
  }

}  // namespace stencil::llm
