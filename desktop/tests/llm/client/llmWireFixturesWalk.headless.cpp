// The provider-wire half of the corpus walk: a capturing MockTransport drives the REAL desktop
// client, and each case's request is deep-compared against the browser's own recorded body.
#include "LlmClient.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

#include "../../support/check.hpp"
#include "../../support/fixtureCorpus.hpp"

using namespace stencil::llm;


  // Captures the request and replies synchronously with the canned response
  // (the tests/LlmClient.headless.cpp mock, trimmed to what the walker needs).
  struct MockTransport : LlmTransport {
    QUrl url;
    QList<QPair<QByteArray, QByteArray>> headers;
    QJsonObject body;
    int status = 200;
    QByteArray response;

    void postJson(const QUrl& u, const QList<QPair<QByteArray, QByteArray>>& h,
                  const QByteArray& b,
                  std::function<void(int, QByteArray, QString)> cb) override {
      url = u;
      headers = h;
      body = QJsonDocument::fromJson(b).object();
      cb(status, response, QString());
    }
    void getJson(const QUrl&, const QList<QPair<QByteArray, QByteArray>>&,
                 std::function<void(int, QByteArray, QString)>) override {}

    QByteArray header(const QByteArray& name) const {
      for (const auto& h : headers)
        if (h.first == name) return h.second;
      return {};
    }
  };

  LlmFailure failureFromKind(const QString& kind) {
    if (kind == "truncated") return LlmFailure::TRUNCATED;
    if (kind == "refusal") return LlmFailure::REFUSAL;
    if (kind == "disabled") return LlmFailure::DISABLED;
    if (kind == "badReply" || kind == "badResponse") return LlmFailure::BAD_RESPONSE;
    if (kind == "expired") return LlmFailure::EXPIRED;
    return LlmFailure::HTTP;
  }

  void walkWireFile(const char* rel, int& walked, int& overridden) {
    std::printf("%s:\n", rel);
    bool ok = false;
    const QJsonArray cases = readJsonFile(corpusPath(rel), &ok).array();
    check(ok && !cases.isEmpty(), qPrintable(QStringLiteral("%1 loads").arg(rel)));

    for (const QJsonValue& cv : cases) {
      const QJsonObject c = cv.toObject();
      const QString name = c.value("name").toString();
      const FixtureOverride ov = findOverride("providerWire", name);
      const QJsonObject ovv = ov.verdict.toObject();
      if (ov.present) ++overridden;
      ++walked;
      const QByteArray label =
          (name + (ov.present ? QStringLiteral(" [override]") : QString())).toUtf8();

      // Settings (fixture provider → the desktop §5 provider keys).
      const QJsonObject settings = c.value("settings").toObject();
      const QString provider = c.value("provider").toString();
      LlmSettings cfg;
      cfg.provider = provider == "openai" ? QStringLiteral("openai-compat")
                     : provider == "server" ? QStringLiteral("stencil-server")
                                            : QStringLiteral("ollama");
      cfg.baseUrl = settings.value("baseUrl").toString();
      cfg.serverUrl = settings.value("serverUrl").toString();
      cfg.model = settings.value("model").toString();
      cfg.apiKey = settings.value("apiKey").toString();
      const QString token = c.value("token").toString();

      // Canonical chat.
      const QJsonObject chat = c.value("chat").toObject();
      const QString fxSystem = chat.value("system").toString();
      QVector<ChatMessage> msgs;
      for (const QJsonValue& mv : chat.value("messages").toArray()) {
        const QJsonObject mo = mv.toObject();
        ChatMessage m;
        m.role = mo.value("role").toString();
        m.text = mo.value("text").toString();
        for (const QJsonValue& iv : mo.value("images").toArray()) {
          const QJsonObject io = iv.toObject();
          m.images.append({io.value("mediaType").toString(),
                           io.value("data").toString().toLatin1()});
        }
        msgs.append(m);
      }

      MockTransport t;
      const QJsonValue errResp = c.value("errorResponse");
      if (errResp.isObject()) {
        t.status = errResp.toObject().value("status").toInt();
        const QJsonValue body = errResp.toObject().value("body");
        t.response = body.isString() ? body.toString().toUtf8()
                                     : QJsonDocument(body.toObject())
                                           .toJson(QJsonDocument::Compact);
      } else {
        t.response =
            QJsonDocument(c.value("response").toObject()).toJson(QJsonDocument::Compact);
      }

      LlmClient client(&t);
      client.setServerTokenResolver([token](const QString&) { return token; });
      LlmReply got;
      client.chat(cfg, msgs, fxSystem, [&](LlmReply r) { got = r; });

      // Request: URL + Authorization (absent in the fixture = must not be sent).
      check(t.url.toString() == c.value("expectUrl").toString(),
            qPrintable(QStringLiteral("%1: request URL").arg(name)));
      const QJsonValue auth = c.value("expectAuthorization");
      check(auth.isString() ? t.header("Authorization") == auth.toString().toUtf8()
                            : t.header("Authorization").isEmpty(),
            qPrintable(QStringLiteral("%1: Authorization header").arg(name)));

      // The captured system must be the canonical prompt + the fixture system
      // (passed as the suffix); substitute the literal before the body compare.
      QJsonObject body = t.body;
      QString capturedSystem;
      if (cfg.provider == "stencil-server") {
        capturedSystem = body.value("system").toString();
        body["system"] = fxSystem;
      } else {
        QJsonArray bm = body.value("messages").toArray();
        QJsonObject sys = bm.at(0).toObject();
        capturedSystem = sys.value("content").toString();
        check(sys.value("role").toString() == "system",
              qPrintable(QStringLiteral("%1: first wire message is the system turn").arg(name)));
        sys["content"] = fxSystem;
        bm.replace(0, sys);
        body["messages"] = bm;
      }
      check(capturedSystem == LlmClient::systemPrompt(fxSystem),
            qPrintable(QStringLiteral("%1: system = canonical prompt + fixture suffix").arg(name)));

      // Body: deep equality; an override's bodyPatch pins a measured desktop
      // extra/difference on top of the shared expectBody.
      QJsonObject expectBody = c.value("expectBody").toObject();
      const QJsonObject patch = ovv.value("bodyPatch").toObject();
      for (auto it = patch.begin(); it != patch.end(); ++it) expectBody[it.key()] = it.value();
      checkJsonEq(body, expectBody,
                  QStringLiteral("%1: request body").arg(QString::fromUtf8(label)));

      // Outcome: extracted reply or typed error. expectError.message is post-BROWSER-sanitizer text, so
      // desktop keeps the kind and must CONTAIN the message, its own errors adding the endpoint tag.
      const QJsonValue expectReply = c.value("expectReply");
      const QJsonObject expectError = c.value("expectError").toObject();
      if (ovv.contains("failure")) {
        check(!got.ok && got.failure == failureFromKind(ovv.value("failure").toString()),
              qPrintable(QStringLiteral("%1: desktop typed failure (override)").arg(name)));
        if (ovv.contains("error"))
          check(got.error.contains(ovv.value("error").toString()),
                qPrintable(QStringLiteral("%1: desktop error text (override)").arg(name)));
      } else if (expectReply.isString()) {
        check(got.ok, qPrintable(QStringLiteral("%1: reply extraction succeeds").arg(name)));
        check(got.text == expectReply.toString(),
              qPrintable(QStringLiteral("%1: extracted reply text").arg(name)));
      } else {
        const QString kind = ovv.contains("kind") ? ovv.value("kind").toString()
                                                  : expectError.value("kind").toString();
        const QString fragment = ovv.contains("message")
                                     ? ovv.value("message").toString()
                                     : expectError.value("message").toString();
        check(!got.ok && got.failure == failureFromKind(kind),
              qPrintable(QStringLiteral("%1: typed error kind \"%2\"%3")
                             .arg(name, kind, ovv.contains("kind") ? " (override)" : "")));
        check(got.error.contains(fragment),
              qPrintable(QStringLiteral("%1: error carries the message").arg(name)));
        if (!got.error.contains(fragment))
          std::printf("       got error: %s\n", qPrintable(got.error));
      }
    }
  }

