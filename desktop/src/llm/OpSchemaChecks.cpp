#include "OpSchema.hpp"
#include "OpSchemaChecks.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

// The registry rides in app.qrc; a pre-main caller can reach it before the
// resource's own global initializer ran (same guard as opRegistry.cpp).
static void ensureAppResources() { Q_INIT_RESOURCE(app); }

namespace stencil::llm {


  bool OpSchema::checkString(const QJsonValue& v, const QJsonObject& spec, const Path& path,
                             const QJsonObject* parent, QString* err) const {
    if (!v.isString()) return fail(err, label(path) + QStringLiteral(" must be a string"));
    const QString raw = v.toString();
    const int max = spec.contains("maxChars")
        ? (spec.value("maxChars").isDouble() ? spec.value("maxChars").toInt()
                                             : limit(spec.value("maxChars").toString()))
        : limit(QStringLiteral("MAX_STRING_CHARS"));
    if (raw.size() > max)
      return fail(err, label(path) + QStringLiteral(" is longer than %1 characters").arg(max));
    const QString s = spec.value("trim").toBool() ? raw.trimmed() : raw;
    if (spec.value("nonEmpty").toBool() && s.trimmed().isEmpty())
      return fail(err, label(path) + QStringLiteral(" must be a non-empty string"));
    if (spec.contains("enum") && !spec.value("enum").toArray().contains(s))
      return fail(err, label(path) + QStringLiteral(" must be one of ") + quoteList(spec.value("enum").toArray()));
    if (spec.value("literals").toArray().contains(s)) return true;
    if (spec.value("blankOk").toBool() && s.trimmed().isEmpty()) return true;
    QStringList names;
    if (spec.value("regex").isString()) names << spec.value("regex").toString();
    for (const QJsonValue& n : spec.value("regex").toArray()) names << n.toString();
    if (spec.contains("regexBy")) {
      const QJsonObject by = spec.value("regexBy").toObject();
      const QString sibling = parent ? parent->value(by.value("key").toString()).toString() : QString();
      const QString mapped = by.value("map").toObject().value(sibling).toString();
      names = mapped.isEmpty() ? QStringList() : QStringList{mapped};
    }
    if (!names.isEmpty()) {
      bool any = false;
      QStringList what;
      for (const QString& n : names) {
        if (regexes_.value(n).match(s).hasMatch()) any = true;
        what << describe(n);
      }
      if (!any) return fail(err, label(path) + QStringLiteral(" must be ") + what.join(QStringLiteral(" or ")));
    }
    if (spec.contains("regexNot")) {
      const QString n = spec.value("regexNot").toString();
      if (regexes_.value(n).match(s).hasMatch())
        return fail(err, label(path) + QStringLiteral(" must be a local value, not ") + describe(n));
    }
    return true;
  }

  bool OpSchema::checkNumber(const QJsonValue& v, const QJsonObject& spec, const Path& path,
                             QString* err) const {
    const bool integer = spec.value("type").toString() == "integer";
    const QString noun = integer ? QStringLiteral("an integer") : QStringLiteral("a number");
    if (!(integer ? isInt(v) : isFiniteNum(v)))
      return fail(err, label(path) + QStringLiteral(" must be ") + noun);
    if (spec.contains("enum") && !spec.value("enum").toArray().contains(v))
      return fail(err, label(path) + QStringLiteral(" must be one of ") + quoteList(spec.value("enum").toArray()));
    if (spec.contains("range")) {
      const QJsonArray r = spec.value("range").toArray();
      const QJsonValue lo = r.at(0), hi = r.at(1);
      const double d = v.toDouble();
      if ((!lo.isNull() && d < lo.toDouble()) || (!hi.isNull() && d > hi.toDouble())) {
        const QString range = !lo.isNull() && !hi.isNull()
            ? QStringLiteral("%1..%2").arg(lo.toDouble()).arg(hi.toDouble())
            : !lo.isNull() ? QStringLiteral(">= %1").arg(lo.toDouble())
                           : QStringLiteral("<= %1").arg(hi.toDouble());
        return fail(err, label(path) + QStringLiteral(" must be ") + noun + QLatin1Char(' ') + range);
      }
    }
    return true;
  }

  bool OpSchema::checkArray(const QJsonValue& v, const QJsonObject& spec, const Path& path,
                            QString* err) const {
    if (!v.isArray()) return fail(err, label(path) + QStringLiteral(" must be an array"));
    const QJsonArray arr = v.toArray();
    const auto bound = [&](const char* key) -> int {
      const QJsonValue b = spec.value(QLatin1String(key));
      if (b.isUndefined() || b.isNull()) return -1;
      return b.isDouble() ? b.toInt() : limit(b.toString());
    };
    const int min = bound("minItems"), max = bound("maxItems");
    if (min == 1 && arr.isEmpty())
      return fail(err, label(path) + QStringLiteral(" must be a non-empty array"));
    const bool window = min > 1 && max >= 0;   // a real N..M window, not just a cap
    const QString held = label(path) + QStringLiteral(" must hold %1..%2 entries").arg(min).arg(max);
    if (max >= 0 && arr.size() > max)
      return fail(err, window ? held : QStringLiteral("more than %1 entries in ").arg(max) + label(path));
    if (min >= 0 && arr.size() < min)
      return fail(err, window ? held : label(path) + QStringLiteral(" must hold at least %1 entries").arg(min));
    if (spec.contains("items")) {
      const QJsonObject items = spec.value("items").toObject();
      for (int i = 0; i < arr.size(); ++i)
        if (!checkValue(arr.at(i), items, item(path, i), nullptr, err)) return false;
    }
    return true;
  }

  bool OpSchema::checkValue(const QJsonValue& v, const QJsonObject& spec, const Path& path,
                            const QJsonObject* parent, QString* err) const {
    const QString type = spec.value("type").toString();
    if (type == "string") return checkString(v, spec, path, parent, err);
    if (type == "integer" || type == "number") return checkNumber(v, spec, path, err);
    if (type == "boolean") {
      if (!v.isBool()) return fail(err, label(path) + QStringLiteral(" must be a boolean"));
      if (spec.contains("enum") && !spec.value("enum").toArray().contains(v))
        return fail(err, label(path) + QStringLiteral(" must be ") + quoteList(spec.value("enum").toArray()));
      return true;
    }
    if (type == "array") return checkArray(v, spec, path, err);
    if (type == "object") {
      if (!v.isObject()) return fail(err, label(path) + QStringLiteral(" must be an object"));
      if (spec.contains("fields") || spec.contains("minFields"))
        return checkFields(v.toObject(), spec.value("fields").toObject(), spec, path, {}, err);
      return true;
    }
    return fail(err, QStringLiteral("opRegistry: unknown type \"%1\"").arg(type));
  }
}  // namespace stencil::llm

