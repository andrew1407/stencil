// Shared plumbing for the conformance-fixture walkers: corpus JSON loading and
// the desktop override map (tests/fixtureOverrides.json). An override pins a
// MEASURED desktop divergence from the shared corpus without editing the
// fixture: key "<family>/<fixture name>" -> { "verdict": <family-specific
// expectation>, "note": "one-line evidence" }.
#pragma once

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <cstdio>

#include "check.hpp"

// STENCIL_CORPUS_DIR (= <repo>/browser/js/config) and STENCIL_OVERRIDES_JSON
// come from the CMake target definitions, like STENCIL_FIXTURES_DIR elsewhere.
inline QString corpusPath(const char* rel) {
  return QStringLiteral(STENCIL_CORPUS_DIR "/") + QString::fromUtf8(rel);
}

inline QJsonDocument readJsonFile(const QString& path, bool* ok = nullptr) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (ok) *ok = false;
    return {};
  }
  QJsonParseError pe{};
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
  if (ok) *ok = pe.error == QJsonParseError::NoError;
  return doc;
}

struct FixtureOverride {
  bool present = false;
  QJsonValue verdict;
  QString note;
};

inline const QJsonObject& fixtureOverrides() {
  static const QJsonObject map = [] {
    bool ok = false;
    const QJsonDocument doc = readJsonFile(QStringLiteral(STENCIL_OVERRIDES_JSON), &ok);
    if (!ok) std::printf("  (no fixtureOverrides.json loaded)\n");
    return doc.object();
  }();
  return map;
}

inline FixtureOverride findOverride(const QString& family, const QString& name) {
  FixtureOverride o;
  const QJsonValue v = fixtureOverrides().value(family + QLatin1Char('/') + name);
  if (!v.isObject()) return o;
  o.present = true;
  o.verdict = v.toObject().value("verdict");
  o.note = v.toObject().value("note").toString();
  return o;
}

inline QByteArray compactJson(const QJsonValue& v) {
  if (v.isObject()) return QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact);
  if (v.isArray()) return QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact);
  if (v.isString()) return ("\"" + v.toString() + "\"").toUtf8();
  if (v.isNull()) return "null";
  if (v.isUndefined()) return "undefined";
  return QByteArray::number(v.toDouble());
}

// check() + a got/want dump on mismatch, so a failing fixture is diagnosable
// straight from the ctest log.
inline void checkJsonEq(const QJsonValue& got, const QJsonValue& want, const QString& msg) {
  const bool ok = got == want;
  check(ok, qPrintable(msg));
  if (!ok) {
    std::printf("       got: %s\n", compactJson(got).constData());
    std::printf("      want: %s\n", compactJson(want).constData());
  }
}
