#include "opSchema.hpp"
#include "opSchemaChecks.hpp"

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

  // One object against a key map + its holder's presence rules. `skip` names keys that
  // are neither declared nor unknown (the action's own "op"). Keys are visited in
  // QJsonObject's sorted order, not declaration order — verdicts are unaffected.
  bool OpSchema::checkFields(const QJsonObject& obj, const QJsonObject& fields,
                             const QJsonObject& holder, const Path& path,
                             const QStringList& skip, QString* err) const {
    // `allowUnknown` (the envelope's variant objects) tolerates undeclared keys;
    // ops and everything else stay strict.
    if (!holder.value("allowUnknown").toBool()) {
      for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (!skip.contains(it.key()) && !fields.contains(it.key()))
          return fail(err, QStringLiteral("unknown field \"%1\"").arg(it.key()) +
                               (path.valid ? QStringLiteral(" in ") + where(path) : QString()));
      }
    }
    if (holder.contains("forms")) {
      const QJsonArray forms = holder.value("forms").toArray();
      QStringList inForms;
      for (const QJsonValue& f : forms)
        for (const QJsonValue& k : f.toArray()) inForms << k.toString();
      QStringList given;
      for (auto it = fields.begin(); it != fields.end(); ++it)
        if (inForms.contains(it.key()) && present(obj, it.key())) given << it.key();
      int matched = 0;
      QStringList shapes;
      for (const QJsonValue& f : forms) {
        const QJsonArray keys = f.toArray();
        bool all = keys.size() == given.size();
        for (const QJsonValue& k : keys) all = all && given.contains(k.toString());
        if (all) ++matched;
        shapes << quoteKeys(keys, QStringLiteral("+"));
      }
      if (matched != 1)
        return fail(err, QStringLiteral("exactly one of ") + shapes.join(QStringLiteral(" / ")) + QStringLiteral(" is required"));
    }
    for (const QJsonValue& g : holder.value("together").toArray()) {
      const QJsonArray group = g.toArray();
      int n = 0;
      for (const QJsonValue& k : group) n += present(obj, k.toString()) ? 1 : 0;
      if (n && n != group.size())
        return fail(err, quoteKeys(group, QStringLiteral(" and ")) + QStringLiteral(" ride together"));
    }
    for (const QJsonValue& g : holder.value("exclusive").toArray()) {
      const QJsonArray group = g.toArray();
      int n = 0;
      for (const QJsonValue& k : group) n += present(obj, k.toString()) ? 1 : 0;
      if (n > 1)
        return fail(err, QStringLiteral("carries both ") + quoteKeys(group, QStringLiteral(" and ")) + QStringLiteral(" — at most one of them"));
    }
    if (holder.contains("minFields")) {
      const int want = holder.value("minFields").toInt();
      int n = 0;
      QStringList names;
      for (auto it = fields.begin(); it != fields.end(); ++it) {
        names << it.key();
        n += present(obj, it.key()) ? 1 : 0;
      }
      if (n < want)
        return fail(err, QStringLiteral("needs at least %1 of %2")
                             .arg(want == 1 ? QStringLiteral("one") : QString::number(want), names.join(QLatin1Char('/'))));
    }
    for (auto it = fields.begin(); it != fields.end(); ++it) {
      const QString k = it.key();
      const QJsonObject spec = it.value().toObject();
      const Path at = child(path, k);
      if (!present(obj, k)) {
        if (spec.value("required").toBool()) return fail(err, label(at) + QStringLiteral(" is required"));
        const QJsonObject rw = spec.value("requiredWith").toObject();
        for (auto d = rw.begin(); d != rw.end(); ++d)
          if (d.value().toArray().contains(obj.value(d.key())))
            return fail(err, label(at) + QStringLiteral(" is required with \"%1\" %2")
                                             .arg(d.key(), quoteList(QJsonArray{obj.value(d.key())})));
        continue;
      }
      const QJsonObject ow = spec.value("onlyWith").toObject();
      for (auto d = ow.begin(); d != ow.end(); ++d)
        if (!d.value().toArray().contains(obj.value(d.key())))
          return fail(err, label(at) + QStringLiteral(" only applies with \"%1\" %2")
                                           .arg(d.key(), quoteKeys(d.value().toArray(), QStringLiteral(" or "))));
      if (!checkValue(obj.value(k), spec, at, &obj, err)) return false;
    }
    return true;
  }

  // normalization: the declared keys only, defaults applied, trims honoured
  QJsonValue OpSchema::pick(const QJsonValue& v, const QJsonObject& spec) const {
    const QString type = spec.value("type").toString();
    if (type == "object" && spec.contains("fields") && v.isObject())
      return pickFields(v.toObject(), spec.value("fields").toObject());
    if (type == "array" && v.isArray()) {
      if (!spec.contains("items")) return v;
      QJsonArray out;
      const QJsonObject items = spec.value("items").toObject();
      for (const QJsonValue& x : v.toArray()) out.append(pick(x, items));
      return out;
    }
    if (type == "string" && spec.value("trim").toBool() && v.isString()) return v.toString().trimmed();
    return v;
  }

  QJsonObject OpSchema::pickFields(const QJsonObject& obj, const QJsonObject& fields) const {
    QJsonObject out;
    for (auto it = fields.begin(); it != fields.end(); ++it) {
      const QJsonObject spec = it.value().toObject();
      if (present(obj, it.key())) out.insert(it.key(), pick(obj.value(it.key()), spec));
      else if (spec.contains("default")) out.insert(it.key(), spec.value("default"));
    }
    return out;
  }


  bool OpSchema::validateAction(const QJsonObject& action, const OpEntry& entry,
                                QJsonObject* validated, QString* err) const {
    QJsonObject v = action;
    QString why;
    bool ok = true;
    for (const QString& rule : entry.rules) {
      if (rule == "cropAspectFold") ok = cropAspectFold(v, &why);
      else ok = fail(&why, QStringLiteral("opRegistry: unknown rule \"%1\"").arg(rule));
      if (!ok) break;
    }
    if (ok) ok = checkFields(v, entry.keys, entry.spec, Path{}, {QStringLiteral("op")}, &why);
    if (!ok) return fail(err, QStringLiteral("Invalid %1 action: %2").arg(entry.name, why));
    if (validated) *validated = v;
    return true;
  }

  QJsonObject OpSchema::normalize(const QJsonObject& validated, const OpEntry& entry) const {
    QJsonObject out = pickFields(validated, entry.keys);
    out.insert("op", entry.name);
    return out;
  }

  bool OpSchema::validateAsk(const QJsonValue& ask, QString* err) const {
    QString why;
    bool ok = ask.isObject();
    if (!ok) why = QStringLiteral("\"ask\" must be an object");
    const QJsonObject schema = registry_.value("ask").toObject().value("schema").toObject();
    if (ok) ok = checkFields(ask.toObject(), schema.value("keys").toObject(), schema,
                             Path{true, QStringLiteral("ask."), QString(), QString()}, {}, &why);
    if (!ok) return fail(err, QStringLiteral("Invalid plan: ") + why);
    return true;
  }

  QJsonObject OpSchema::normalizeAsk(const QJsonObject& ask) const {
    return pickFields(ask, registry_.value("ask").toObject().value("schema").toObject()
                               .value("keys").toObject());
  }

  bool OpSchema::checkEnvelope(const QJsonValue& v, const QString& key, QString* err) const {
    QString why;
    if (!checkValue(v, registry_.value("envelope").toObject().value(key).toObject(),
                    Path{true, QString(), key, QString()}, nullptr, &why))
      return fail(err, QStringLiteral("Invalid plan: ") + why);
    return true;
  }
}  // namespace stencil::llm

