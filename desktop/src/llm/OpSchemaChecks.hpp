#pragma once
// The op-schema validation helpers — presence, number shapes and message paths —
// private to the OpSchema*.cpp TUs.
#include "OpSchema.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <cmath>

namespace stencil::llm {

  // Where a value sits, for messages: `"x1" in spec`, `"label" in ask.options[2]`.
  // `valid` false = no path (a top-level action).
  struct OpSchema::Path {
    bool valid = false;
    QString root;
    QString key;
    QString container;
  };


  inline bool present(const QJsonObject& o, const QString& k) {
    const QJsonValue v = o.value(k);
    return !v.isUndefined() && !v.isNull();
  }

  inline bool fail(QString* err, const QString& why) {
    if (err) *err = why;
    return false;
  }

  inline QString where(const OpSchema::Path& p) {
    if (!p.key.isEmpty())
      return (p.container.isEmpty() ? QString() : p.container + QLatin1Char('.')) + p.root + p.key;
    QString r = p.root;
    if (r.endsWith(QLatin1Char('.'))) r.chop(1);
    return r;
  }

  inline QString label(const OpSchema::Path& p) {
    return QLatin1Char('"') + p.root + p.key + QLatin1Char('"') +
           (p.container.isEmpty() ? QString() : QStringLiteral(" in ") + p.container);
  }

  inline OpSchema::Path child(const OpSchema::Path& p, const QString& key) {
    if (p.valid && !p.key.isEmpty()) return {true, QString(), key, where(p)};
    return {true, p.valid ? p.root : QString(), key, QString()};
  }

  inline OpSchema::Path item(const OpSchema::Path& p, int i) {
    return {true, p.root, p.key + QLatin1Char('[') + QString::number(i) + QLatin1Char(']'),
            p.container};
  }

  inline QString quoteList(const QJsonArray& xs) {
    QStringList out;
    for (const QJsonValue& x : xs) {
      if (x.isString()) out << QLatin1Char('"') + x.toString() + QLatin1Char('"');
      else if (x.isBool()) out << (x.toBool() ? QStringLiteral("true") : QStringLiteral("false"));
      else if (x.isDouble()) out << QString::number(x.toDouble());
      else out << QStringLiteral("null");
    }
    return out.join(QStringLiteral(", "));
  }

  inline QString quoteKeys(const QJsonArray& keys, const QString& sep) {
    QStringList out;
    for (const QJsonValue& k : keys) out << QLatin1Char('"') + k.toString() + QLatin1Char('"');
    return out.join(sep);
  }

  inline bool isFiniteNum(const QJsonValue& v) { return v.isDouble() && std::isfinite(v.toDouble()); }
  inline bool isInt(const QJsonValue& v) {
    return isFiniteNum(v) && std::floor(v.toDouble()) == v.toDouble();
  }

  // native cross-field rules an entry may name in `rules`
  // §3.2 tolerance: "aspect" beside "spec" folds into the spec when it lacks one; a
  // conflicting duplicate fails. The folded copy is what gets validated + normalized.
  inline bool cropAspectFold(QJsonObject& a, QString* err) {
    if (!present(a, "aspect") || !a.value("spec").isObject()) return true;
    QJsonObject spec = a.value("spec").toObject();
    if (present(spec, "aspect") && spec.value("aspect") != a.value("aspect"))
      return fail(err, QStringLiteral("\"aspect\" appears both beside \"spec\" and inside it with different values"));
    if (!present(spec, "aspect")) spec.insert("aspect", a.value("aspect"));
    a.remove("aspect");
    a.insert("spec", spec);
    return true;
  }


}  // namespace stencil::llm
