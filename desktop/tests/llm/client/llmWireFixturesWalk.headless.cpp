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
  // (the tests/llm/client/LlmClient.headless.cpp mock, trimmed to what the walker needs).
  struct MockTransport : LlmTransport {
    bool posted = false;
    QUrl url;
    QList<QPair<QByteArray, QByteArray>> headers;
    QJsonObject body;
    int status = 200;
    QByteArray response;

    void postJson(const QUrl& u, const QList<QPair<QByteArray, QByteArray>>& h,
                  const QByteArray& b,
                  std::function<void(int, QByteArray, QString)> cb) override {
      posted = true;
      url = u;
      headers = h;
      body = QJsonDocument::fromJson(b).object();
      cb(status, response, QString());
    }
    void getJson(const QUrl&, const QList<QPair<QByteArray, QByteArray>>&,
                 std::function<void(int, QByteArray, QString)>) override {}

    bool has(const QByteArray& name) const {
      for (const auto& h : headers)
        if (h.first.toLower() == name.toLower()) return true;
      return false;
    }
    QByteArray header(const QByteArray& name) const {
      for (const auto& h : headers)
        if (h.first.toLower() == name.toLower()) return h.second;
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

  // The fixture's provider → the desktop's §5 provider key.
  QString providerOf(const QString& wire) {
    if (wire == "openai") return QStringLiteral("openai-compat");
    if (wire == "server") return QStringLiteral("stencil-server");
    if (wire == "anthropic") return QStringLiteral("anthropic");
    return QStringLiteral("ollama");
  }

  // Headers, the system turn and the body of a request the case expects to be sent.
  void checkRequest(const QJsonObject& c, const LlmSettings& cfg, const MockTransport& t,
                    const QString& fxSystem, const QJsonObject& ovv, const QString& name,
                    const QString& label) {
    check(t.url.toString() == c.value("expectUrl").toString(),
          qPrintable(QStringLiteral("%1: request URL").arg(name)));
    const QJsonValue auth = c.value("expectAuthorization");
    check(auth.isString() ? t.header("Authorization") == auth.toString().toUtf8()
                          : !t.has("Authorization"),
          qPrintable(QStringLiteral("%1: Authorization header").arg(name)));
    const QJsonObject expectHeaders = c.value("expectHeaders").toObject();
    for (auto it = expectHeaders.begin(); it != expectHeaders.end(); ++it)
      check(t.header(it.key().toUtf8()) == it.value().toString().toUtf8(),
            qPrintable(QStringLiteral("%1: header %2").arg(name, it.key())));
    // A desktop app is no web page: the browser-only direct-access header is never sent.
    check(!t.has("anthropic-dangerous-direct-browser-access"),
          qPrintable(QStringLiteral("%1: no direct-browser-access header").arg(name)));

    // The captured system must be the canonical prompt + the fixture system
    // (passed as the suffix); substitute the literal before the body compare.
    QJsonObject body = t.body;
    QString capturedSystem;
    if (cfg.provider == "stencil-server" || cfg.provider == "anthropic") {
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
    checkJsonEq(body, expectBody, QStringLiteral("%1: request body").arg(label));
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
      const QString label = name + (ov.present ? QStringLiteral(" [override]") : QString());

      const QJsonObject settings = c.value("settings").toObject();
      LlmSettings cfg;
      cfg.provider = providerOf(c.value("provider").toString());
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

      if (c.value("expectNoRequest").toBool())
        check(!t.posted, qPrintable(QStringLiteral("%1: nothing is sent").arg(name)));
      else
        checkRequest(c, cfg, t, fxSystem, ovv, name, label);

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
        // anthropic: the tag is the only addition, so the reason ENDS the text, never a vetoed detail.
        const bool carried = cfg.provider == "anthropic" ? got.error.endsWith(fragment)
                                                         : got.error.contains(fragment);
        check(carried, qPrintable(QStringLiteral("%1: error carries the message").arg(name)));
        if (!carried) std::printf("       got error: %s\n", qPrintable(got.error));
        for (int i = 0; i + 8 <= cfg.apiKey.size(); ++i)
          if (got.error.contains(cfg.apiKey.mid(i, 8)))
            check(false, qPrintable(QStringLiteral("%1: the error echoes the key").arg(name)));
      }
    }
  }
