#pragma once
// The config-canon suite's sections, one TU each behind this header: the shared config JSON the
// desktop reads through its resources/app.qrc aliases.
#include <QFile>
#include <QJsonDocument>

inline QJsonDocument readConfig(const char* res) {
  QFile f(res);
  if (!f.open(QIODevice::ReadOnly)) return QJsonDocument();
  return QJsonDocument::fromJson(f.readAll());
}

// llm/systemPrompt.json, llm/opRegistry.json, llm/providers.json (configCanonLlm.headless.cpp).
void checkLlmCanon();
