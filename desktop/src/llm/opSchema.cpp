#include "opSchema.hpp"

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

  // Where a value sits, for messages: `"x1" in spec`, `"label" in ask.options[2]`.
  // `valid` false = no path (a top-level action).
  struct OpSchema::Path {
    bool valid = false;
    QString root;
    QString key;
    QString container;
  };

  namespace {

    bool present(const QJsonObject& o, const QString& k) {
      const QJsonValue v = o.value(k);
      return !v.isUndefined() && !v.isNull();
    }

    bool fail(QString* err, const QString& why) {
      if (err) *err = why;
      return false;
    }

    QString where(const OpSchema::Path& p) {
      if (!p.key.isEmpty())
        return (p.container.isEmpty() ? QString() : p.container + QLatin1Char('.')) + p.root + p.key;
      QString r = p.root;
      if (r.endsWith(QLatin1Char('.'))) r.chop(1);
      return r;
    }

    QString label(const OpSchema::Path& p) {
      return QLatin1Char('"') + p.root + p.key + QLatin1Char('"') +
             (p.container.isEmpty() ? QString() : QStringLiteral(" in ") + p.container);
    }

    OpSchema::Path child(const OpSchema::Path& p, const QString& key) {
      if (p.valid && !p.key.isEmpty()) return {true, QString(), key, where(p)};
      return {true, p.valid ? p.root : QString(), key, QString()};
    }

    OpSchema::Path item(const OpSchema::Path& p, int i) {
      return {true, p.root, p.key + QLatin1Char('[') + QString::number(i) + QLatin1Char(']'),
              p.container};
    }

    QString quoteList(const QJsonArray& xs) {
      QStringList out;
      for (const QJsonValue& x : xs) {
        if (x.isString()) out << QLatin1Char('"') + x.toString() + QLatin1Char('"');
        else if (x.isBool()) out << (x.toBool() ? QStringLiteral("true") : QStringLiteral("false"));
        else if (x.isDouble()) out << QString::number(x.toDouble());
        else out << QStringLiteral("null");
      }
      return out.join(QStringLiteral(", "));
    }

    QString quoteKeys(const QJsonArray& keys, const QString& sep) {
      QStringList out;
      for (const QJsonValue& k : keys) out << QLatin1Char('"') + k.toString() + QLatin1Char('"');
      return out.join(sep);
    }

    bool isFiniteNum(const QJsonValue& v) { return v.isDouble() && std::isfinite(v.toDouble()); }
    bool isInt(const QJsonValue& v) {
      return isFiniteNum(v) && std::floor(v.toDouble()) == v.toDouble();
    }

    // native cross-field rules an entry may name in `rules`
    // §3.2 tolerance: "aspect" beside "spec" folds into the spec when it lacks one; a
    // conflicting duplicate fails. The folded copy is what gets validated + normalized.
    bool cropAspectFold(QJsonObject& a, QString* err) {
      if (!present(a, "aspect") || !a.value("spec").isObject()) return true;
      QJsonObject spec = a.value("spec").toObject();
      if (present(spec, "aspect") && spec.value("aspect") != a.value("aspect"))
        return fail(err, QStringLiteral("\"aspect\" appears both beside \"spec\" and inside it with different values"));
      if (!present(spec, "aspect")) spec.insert("aspect", a.value("aspect"));
      a.remove("aspect");
      a.insert("spec", spec);
      return true;
    }

  }  // namespace

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
      return Opset::Entry;
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
        return Opset::Entry;
      }
      return Opset::Unknown;
    }
    if (os.value("failOps").toArray().contains(op)) return Opset::Fail;
    return Opset::Unknown;
  }

}  // namespace stencil::llm
