#pragma once
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>

// Registry-driven op-plan validation (llm-contract.md §1-§2, §11, §13) - the desktop port of
// browser/js/llm/plan/schema.js, table-driven from the shared browser/js/config/llm/opRegistry.json
// (qrc alias llm/opRegistry.json): profile membership, unknown-field rejection, required keys,
// types, enums, ranges, string caps, token grammars and the cross-field presence rules all come
// from the registry. Ported op-for-op; only the error wording is this surface's.
namespace stencil::llm {

  // One registry entry resolved for this surface: its key schema (surfaceKeys wins),
  // prompt bullet (bulletVariants wins) and flags (surfaceFlags merged over flags).
  struct OpEntry {
    QString name;
    QJsonObject keys;
    QJsonObject spec;       // the raw entry (forms / together / exclusive / minFields …)
    QJsonObject flags;
    QStringList rules;
    QString bullet;         // empty when it rides a sibling's (bulletSharedWith)
    QString bulletSharedWith;
    QString also;           // the §10 "also accepts" line, ordered by alsoOrder
    int alsoOrder = 0;
    QStringList requires;
    bool flag(const char* name) const { return flags.value(QLatin1String(name)).toBool(); }
  };

  class OpSchema {
  public:
    // The desktop schema (surface "desktop", profile "editor"), parsed once from the
    // qrc registry. A broken alias yields an empty schema — the tests fail fast on it.
    static const OpSchema& desktop();
    OpSchema(const QJsonObject& registry, const QString& surface);

    const QString& getSurface() const { return surface; }
    const QString& getProfile() const { return profile; }
    // This surface's entries in the profile's (= prompt) order.
    const QVector<OpEntry>& getEntries() const { return entries; }
    const OpEntry* entry(const QString& op) const;
    // §13 forbidden op names (forbidden.perSurface.<surface>), registry order.
    const QStringList& getForbidden() const { return forbidden; }
    // A cap by name or dotted path ("MAX_ACTIONS", "ask.label"); -1 when unknown.
    int limit(const QString& name) const;
    QString defaultCustomLabel() const;

    // Validate one action against its entry (native rules first). *validated is the action as
    // validated (post-fold) - feed it to normalize(). Errors carry the "Invalid <op> action: " prefix.
    bool validateAction(const QJsonObject& action, const OpEntry& entry,
                        QJsonObject* validated, QString* err) const;
    // The declared keys present (deep-picked, trims honoured) plus defaults.
    QJsonObject normalize(const QJsonObject& validated, const OpEntry& entry) const;
    // The §11 card's structure (option `actions` only shallowly — the caller validates
    // them as preview actions). Errors carry the "Invalid plan: " prefix.
    bool validateAsk(const QJsonValue& ask, QString* err) const;
    QJsonObject normalizeAsk(const QJsonObject& ask) const;
    // One envelope slot ("actions" / "variants") checked shallowly ("Invalid plan: ").
    bool checkEnvelope(const QJsonValue& v, const QString& key, QString* err) const;

    // Resolve an op inside a nested op set (§8 open.actions): an entry to validate
    // with, Fail for a listed-but-disallowed op, Unknown for anything else.
    enum class Opset { ENTRY, FAIL, UNKNOWN };
    Opset opsetEntry(const QString& opset, const QString& op, OpEntry* out) const;

    // Where a value sits, for messages (`"x1" in spec`); an engine detail.
    struct Path;

  private:
    bool checkValue(const QJsonValue& v, const QJsonObject& spec, const Path& path,
                    const QJsonObject* parent, QString* err) const;
    bool checkString(const QJsonValue& v, const QJsonObject& spec, const Path& path,
                     const QJsonObject* parent, QString* err) const;
    bool checkNumber(const QJsonValue& v, const QJsonObject& spec, const Path& path,
                     QString* err) const;
    bool checkArray(const QJsonValue& v, const QJsonObject& spec, const Path& path,
                    QString* err) const;
    bool checkFields(const QJsonObject& obj, const QJsonObject& fields,
                     const QJsonObject& holder, const Path& path, const QStringList& skip,
                     QString* err) const;
    QJsonValue pick(const QJsonValue& v, const QJsonObject& spec) const;
    QJsonObject pickFields(const QJsonObject& obj, const QJsonObject& fields) const;
    QString describe(const QString& regexName) const;

    QJsonObject registry;
    QString surface;
    QString profile;
    QJsonObject limits;
    QMap<QString, QRegularExpression> regexes;
    QVector<OpEntry> entries;
    QStringList forbidden;
  };

}  // namespace stencil::llm
