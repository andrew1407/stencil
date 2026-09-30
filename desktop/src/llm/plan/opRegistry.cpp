#include "opRegistry.hpp"
#include "OpPlanSchema.hpp"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtGlobal>

#include <algorithm>

// The §4 prose core rides in app.qrc (the shared canon
// common/config/llm/systemPrompt.json). A pre-main caller can reach it
// before the resource's own global initializer ran, so force registration on
// first read — same guard as theme.cpp. Global scope: Q_INIT_RESOURCE declares
// the generated init function.
static void ensureAppResources() { Q_INIT_RESOURCE(app); }

// The desktop op registry (llm-contract §13): core's resolution of the shared opRegistry.json
// supplies every bullet, flag, forbidden name and cap; this table adds the OpKind and the capability.
namespace stencil::llm {

  namespace {
    const QByteArray& registryBytes() {
      static const QByteArray bytes = [] {
        ensureAppResources();
        QFile f(QStringLiteral(":/config/llm/opRegistry.json"));
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
      }();
      return bytes;
    }

    // A raw registry op, for the prompt-only fields core does not resolve (bulletSharedWith, also).
    QJsonObject registryOp(const QString& name) {
      static const QJsonArray ops = QJsonDocument::fromJson(registryBytes()).object().value("ops").toArray();
      for (const QJsonValue& v : ops)
        if (v.toObject().value("name").toString() == name) return v.toObject();
      return QJsonObject();
    }

    QJsonObject surfaceEntry(const QString& name) {
      for (const QJsonValue& v : planSurface().value("entries").toArray())
        if (v.toObject().value("name").toString() == name) return v.toObject();
      return QJsonObject();
    }
  }  // namespace

  const model::OpPlanSchema& planSchema() {
    static const model::OpPlanSchema schema(registryBytes(), "desktop");
    return schema;
  }

  const QJsonObject& planSurface() {
    static const QJsonObject surface = QJsonDocument::fromJson(planSchema().entriesJson()).object();
    return surface;
  }

  int planLimit(const QString& name) {
    QJsonValue v = planSurface().value("limits");
    for (const QString& k : name.split(QLatin1Char('.'))) v = v.toObject().value(k);
    return v.isDouble() ? v.toInt() : -1;
  }

  // Any string field of the §4 prompt canon, parsed once from the qrc asset. "head" carries its
  // trailing newline and "tail" its leading blank line, so assembly is head + bullets + tail.
  QString promptText(const QString& key) {
    static const QJsonObject canon = [] {
      ensureAppResources();
      QFile f(QStringLiteral(":/config/llm/systemPrompt.json"));
      if (!f.open(QIODevice::ReadOnly)) return QJsonObject();
      return QJsonDocument::fromJson(f.readAll()).object();
    }();
    return canon.value(key).toString();
  }

  namespace {
    struct OpRow { OpKind kind; const char* name; unsigned capability; };

    // Emission order = final prompt order: the §2 core ops, then the §10 editor
    // block spliced between the frame and image bullets.
    constexpr OpRow ROWS[] = {
        {OpKind::CROP, "crop", CAP_NONE},
        {OpKind::ROTATE, "rotate", CAP_NONE},
        {OpKind::FILTER, "filter", CAP_NONE},
        {OpKind::LAYOUT, "layout", CAP_NONE},
        {OpKind::FORMULA, "formula", CAP_NONE},
        {OpKind::PAGE, "page", CAP_NONE},
        {OpKind::BLANK, "blank", CAP_NONE},
        {OpKind::UNDO, "undo", CAP_NONE},
        {OpKind::REDO, "redo", CAP_NONE},
        {OpKind::FRAME, "frame", CAP_VIDEO},
        {OpKind::THEME, "theme", CAP_NONE},
        {OpKind::ACCENT, "accent", CAP_NONE},
        {OpKind::LINE_STYLE, "lineStyle", CAP_NONE},
        {OpKind::UNITS, "units", CAP_NONE},
        {OpKind::VIEW, "view", CAP_NONE},
        {OpKind::CLEAR, "clear", CAP_NONE},
        {OpKind::OPEN_URL, "openUrl", CAP_NONE},
        {OpKind::OPEN_FILE, "openFile", CAP_FILESYSTEM},
        {OpKind::CONNECT, "connect", CAP_SERVERS},
        {OpKind::DISCONNECT, "disconnect", CAP_SERVERS},
        {OpKind::COPY, "copy", CAP_CLIPBOARD},
        {OpKind::REMOVE_PROJECT, "removeProject", CAP_NONE},
        {OpKind::CLEAR_PROJECTS, "clearProjects", CAP_NONE},
        {OpKind::COMPARE, "compare", CAP_NONE},
        {OpKind::ZOOM, "zoom", CAP_NONE},
        {OpKind::RENAME_PROJECT, "renameProject", CAP_NONE},
        {OpKind::PROJECT_COLOR, "projectColor", CAP_NONE},
        {OpKind::BLANK_COLOR, "blankColor", CAP_NONE},
        {OpKind::OPEN_PROJECT, "openProject", CAP_NONE},
        {OpKind::INCOGNITO, "incognito", CAP_NONE},
        {OpKind::COPY_PROJECT, "copyProject", CAP_NONE},
        {OpKind::CHAT_PANEL, "chatPanel", CAP_NONE},
        {OpKind::DIALOG, "dialog", CAP_NONE},
        {OpKind::CLEAR_CHAT, "clearChat", CAP_NONE},
        {OpKind::IMAGE, "image", CAP_NONE},
        {OpKind::SAVE, "save", CAP_NONE},
    };
  }  // namespace

  // Bullets and flags are the resolved entry's (undo/redo and connect/disconnect share one bullet
  // each through bulletSharedWith — assembly emits it once).
  const QVector<OpDescriptor>& opRegistry() {
    static const QVector<OpDescriptor> table = [] {
      QVector<OpDescriptor> out;
      for (const OpRow& r : ROWS) {
        OpDescriptor d{r.kind, r.name, QString(), false, false, false, r.capability};
        const QString name = QString::fromUtf8(r.name);
        const QJsonObject e = surfaceEntry(name);
        if (!e.isEmpty()) {
          const QString shared = registryOp(name).value("bulletSharedWith").toString();
          d.bullet = (shared.isEmpty() ? e : surfaceEntry(shared)).value("bullet").toString();
          const QJsonObject flags = e.value("flags").toObject();
          d.editorSettings = flags.value("editorSetting").toBool();
          d.topLevelOnly = d.editorSettings || flags.value("topLevelOnly").toBool();
          d.history = r.kind == OpKind::UNDO || r.kind == OpKind::REDO;
        }
        out.append(d);
      }
      return out;
    }();
    return table;
  }

  // §10 "also accepts" widenings - the registry's `also` lines in alsoOrder, closing the editor
  // block; each dropped with its op when the op's capability is missing.
  const QVector<OpAddendum>& opAddenda() {
    static const QVector<OpAddendum> addenda = [] {
      QVector<QPair<int, OpAddendum>> ordered;
      for (const QJsonValue& v : planSurface().value("entries").toArray()) {
        const QString name = v.toObject().value("name").toString();
        const QJsonObject raw = registryOp(name);
        const QString also = raw.value("also").toString();
        OpKind kind;
        if (also.isEmpty() || !opKindFor(name, &kind)) continue;
        ordered.append({raw.value("alsoOrder").toInt(), OpAddendum{kind, also}});
      }
      std::stable_sort(ordered.begin(), ordered.end(),
                       [](const auto& a, const auto& b) { return a.first < b.first; });
      QVector<OpAddendum> out;
      for (const auto& p : ordered) out.append(p.second);
      return out;
    }();
    return addenda;
  }

  QString opName(OpKind kind) {
    for (const OpRow& r : ROWS)
      if (r.kind == kind) return QString::fromUtf8(r.name);
    return QString();
  }

  bool opKindFor(const QString& name, OpKind* out) {
    for (const OpRow& r : ROWS) {
      if (name != QLatin1String(r.name)) continue;
      if (out) *out = r.kind;
      return true;
    }
    return false;
  }

  QStringList forbiddenOps() {
    // The registry's forbidden.perSurface.desktop. Never registered; the
    // registry test and the executor reject are the two enforcement teeth.
    static const QStringList names = [] {
      QStringList out;
      for (const QJsonValue& v : planSurface().value("forbidden").toArray()) out << v.toString();
      return out;
    }();
    return names;
  }

  bool isForbiddenOpName(const QString& name) {
    for (const QString& f : forbiddenOps())
      if (name.compare(f, Qt::CaseInsensitive) == 0) return true;
    return false;
  }

  bool rejectForbiddenOp(const QString& name, QString* err) {
    if (!isForbiddenOpName(name)) return false;
    if (err) *err = QStringLiteral("%1: this operation is not model-drivable").arg(name);
    return true;
  }

}  // namespace stencil::llm
