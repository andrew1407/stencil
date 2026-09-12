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

  const OpSchema& OpSchema::desktop() {
    static const OpSchema schema = [] {
      ensureAppResources();
      QJsonObject registry;
      QFile f(QStringLiteral(":/config/llm/opRegistry.json"));
      if (f.open(QIODevice::ReadOnly)) registry = QJsonDocument::fromJson(f.readAll()).object();
      return OpSchema(registry, QStringLiteral("desktop"));
    }();
    return schema;
  }

  OpSchema::OpSchema(const QJsonObject& registry, const QString& surface)
      : registry_(registry), surface_(surface) {
    profile_ = registry.value("$meta").toObject().value("surfaceProfiles").toObject()
                   .value(surface).toString();
    limits_ = registry.value("limits").toObject();
    const QJsonObject regexes = registry.value("regexes").toObject();
    for (auto it = regexes.begin(); it != regexes.end(); ++it) {
      if (it.key() == "describe" || it.key() == "note") continue;
      // Flag-free sources compiled as-is, except that PCRE2's `$` also matches before a
      // final newline where JS's does not — pin it to the true end.
      QString src = it.value().toString();
      if (src.endsWith(QLatin1Char('$')) && !src.endsWith(QStringLiteral("\\$")))
        src = src.chopped(1) + QStringLiteral("\\z");
      regexes_.insert(it.key(), QRegularExpression(src));
    }
    for (const QJsonValue& f : registry.value("forbidden").toObject().value("perSurface")
                                   .toObject().value(surface).toArray())
      forbidden_ << f.toString();

    // The entries this surface registers: its profile's ops in the profile's (= prompt)
    // order, minus entries restricted to other surfaces, each resolved for this surface.
    QStringList order;
    for (const QJsonValue& n : registry.value("profiles").toObject().value(profile_)
                                   .toObject().value("ops").toArray())
      order << n.toString();
    QVector<OpEntry> found;
    for (const QJsonValue& ev : registry.value("ops").toArray()) {
      const QJsonObject e = ev.toObject();
      bool inProfile = false;
      for (const QJsonValue& p : e.value("profiles").toArray())
        if (p.toString() == profile_) inProfile = true;
      if (!inProfile) continue;
      if (e.contains("surfaces")) {
        bool mine = false;
        for (const QJsonValue& s : e.value("surfaces").toArray())
          if (s.toString() == surface) mine = true;
        if (!mine) continue;
      }
      const auto forSurface = [&](const char* map) -> QJsonValue {
        const QJsonObject m = e.value(QLatin1String(map)).toObject();
        if (m.contains(surface)) return m.value(surface);
        if (m.contains(profile_)) return m.value(profile_);
        return QJsonValue::Undefined;
      };
      OpEntry entry;
      entry.name = e.value("name").toString();
      entry.spec = e;
      const QJsonObject sk = e.value("surfaceKeys").toObject();
      entry.keys = sk.contains(surface) ? sk.value(surface).toObject() : e.value("keys").toObject();
      const QJsonValue variant = forSurface("bulletVariants");
      entry.bullet = variant.isString() ? variant.toString() : e.value("bullet").toString();
      entry.bulletSharedWith = e.value("bulletSharedWith").toString();
      entry.flags = e.value("flags").toObject();
      const QJsonObject extra = forSurface("surfaceFlags").toObject();
      for (auto it = extra.begin(); it != extra.end(); ++it) entry.flags.insert(it.key(), it.value());
      for (const QJsonValue& r : e.value("rules").toArray()) entry.rules << r.toString();
      entry.also = e.value("also").toString();
      entry.alsoOrder = e.value("alsoOrder").toInt();
      for (const QJsonValue& r : e.value("requires").toArray()) entry.requires << r.toString();
      found.append(entry);
    }
    std::stable_sort(found.begin(), found.end(), [&](const OpEntry& a, const OpEntry& b) {
      return order.indexOf(a.name) < order.indexOf(b.name);
    });
    entries_ = found;
  }

  const OpEntry* OpSchema::entry(const QString& op) const {
    for (const OpEntry& e : entries_)
      if (e.name == op) return &e;
    return nullptr;
  }

  int OpSchema::limit(const QString& name) const {
    QJsonValue v = limits_;
    for (const QString& k : name.split(QLatin1Char('.'))) v = v.toObject().value(k);
    return v.isDouble() ? v.toInt() : -1;
  }

  QString OpSchema::defaultCustomLabel() const {
    return registry_.value("ask").toObject().value("defaultCustomLabel").toString();
  }

  QString OpSchema::describe(const QString& regexName) const {
    const QString d = registry_.value("regexes").toObject().value("describe").toObject()
                          .value(regexName).toString();
    return d.isEmpty() ? regexName : d;
  }

  OpSchema::Opset OpSchema::opsetEntry(const QString& opset, const QString& op,
                                       OpEntry* out) const {
    const QJsonObject os = registry_.value("opsets").toObject().value(opset).toObject();
    const QJsonObject overrides = os.value("overrides").toObject();
    if (overrides.contains(op)) {
      const QJsonObject o = overrides.value(op).toObject();
      if (out) {
        *out = OpEntry();
        out->name = op;
        out->keys = o.value("keys").toObject();
        out->spec = o;
        for (const QJsonValue& r : o.value("rules").toArray()) out->rules << r.toString();
      }
      return Opset::ENTRY;
    }
    if (os.value("ops").toArray().contains(op)) {
      for (const QJsonValue& ev : registry_.value("ops").toArray()) {
        const QJsonObject e = ev.toObject();
        if (e.value("id").toString() != op) continue;
        if (out) {
          *out = OpEntry();
          out->name = op;
          out->keys = e.value("keys").toObject();
          out->spec = e;
          for (const QJsonValue& r : e.value("rules").toArray()) out->rules << r.toString();
        }
        return Opset::ENTRY;
      }
      return Opset::UNKNOWN;
    }
    if (os.value("failOps").toArray().contains(op)) return Opset::FAIL;
    return Opset::UNKNOWN;
  }
}  // namespace stencil::llm

