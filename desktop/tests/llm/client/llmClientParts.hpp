#pragma once
// The LlmClient suite's sections, one TU each behind this header, called in this order from
// main(). Every one drives the real client over a MockTransport, so nothing touches a network.
#include "LlmClient.hpp"
#include "llmSettings.hpp"
#include "opPlan.hpp"
#include "opRegistry.hpp"
#include "../../support/check.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <cstdio>

namespace llmclient {

  using namespace stencil::llm;

// ollama is no longer the default provider (llmSettings.hpp ships "none").
static LlmSettings ollamaCfg() {
  LlmSettings c; c.provider = "ollama"; c.baseUrl = defaultLlmBaseUrl("ollama"); return c;
}

// Captures the request and replies synchronously with the canned response.
struct MockTransport : LlmTransport {
  QUrl url;
  QString method;  // "POST" | "GET" (last request)
  QList<QPair<QByteArray, QByteArray>> headers;
  QJsonObject body;
  int status = 200;
  QByteArray response;
  QString transportError;

  void postJson(const QUrl& u, const QList<QPair<QByteArray, QByteArray>>& h,
                const QByteArray& b,
                std::function<void(int, QByteArray, QString)> cb) override {
    url = u;
    method = "POST";
    headers = h;
    body = QJsonDocument::fromJson(b).object();
    cb(status, response, transportError);
  }

  void getJson(const QUrl& u, const QList<QPair<QByteArray, QByteArray>>& h,
               std::function<void(int, QByteArray, QString)> cb) override {
    url = u;
    method = "GET";
    headers = h;
    body = QJsonObject();
    cb(status, response, transportError);
  }

  QByteArray header(const QByteArray& name) const {
    for (const auto& h : headers)
      if (h.first == name) return h.second;
    return {};
  }
};

static QVector<ChatMessage> sampleMessages() {
  ChatMessage prior;
  prior.role = "assistant";
  prior.text = "prior reply";
  ChatMessage user;
  user.role = "user";
  user.text = "make it sepia";
  user.images.append({QStringLiteral("image/png"), QByteArrayLiteral("QUJD")});  // "ABC"
  return {prior, user};
}

  void checkOllamaAndOpenAi();
  void checkStencilServer();
  void checkProbe();
  void checkModelsAndErrors();
  void checkOpRegistry();
  void checkPromptAssembly();
  void checkPromptShape();

}  // namespace llmclient
