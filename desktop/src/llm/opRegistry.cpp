#include "opRegistry.hpp"
#include "OpSchema.hpp"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QtGlobal>

#include <algorithm>

// The §4 prose core rides in app.qrc (the shared canon
// browser/js/config/llm/systemPrompt.json). A pre-main caller can reach it
// before the resource's own global initializer ran, so force registration on
// first read — same guard as theme.cpp. Global scope: Q_INIT_RESOURCE declares
// the generated init function.
static void ensureAppResources() { Q_INIT_RESOURCE(app); }

// The desktop op registry (llm-contract §13): the shared opRegistry.json supplies every bullet,
// "also accepts" line, flag and forbidden name; this table adds the OpKind and the capability.
namespace stencil::llm {

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
        {OpKind::CHAT_PANEL, "chatPanel", CAP_NONE},
        {OpKind::DIALOG, "dialog", CAP_NONE},
        {OpKind::CLEAR_CHAT, "clearChat", CAP_NONE},
        {OpKind::IMAGE, "image", CAP_NONE},
        {OpKind::SAVE, "save", CAP_NONE},
    };
  }  // namespace

  // Bullets and flags are the registry entry's (undo/redo and connect/disconnect
  // share one bullet each through bulletSharedWith — assembly emits it once).
  const QVector<OpDescriptor>& opRegistry() {
    static const QVector<OpDescriptor> table = [] {
      const OpSchema& schema = OpSchema::desktop();
      QVector<OpDescriptor> out;
      for (const OpRow& r : ROWS) {
        OpDescriptor d{r.kind, r.name, QString(), false, false, false, r.capability};
        if (const OpEntry* e = schema.entry(QLatin1String(r.name))) {
          const OpEntry* src = e->bulletSharedWith.isEmpty() ? e : schema.entry(e->bulletSharedWith);
          if (src) d.bullet = src->bullet;
          d.editorSettings = e->flag("editorSetting");
          d.topLevelOnly = d.editorSettings || e->flag("topLevelOnly");
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
      for (const OpEntry& e : OpSchema::desktop().getEntries()) {
        OpKind kind;
        if (e.also.isEmpty() || !opKindFor(e.name, &kind)) continue;
        ordered.append({e.alsoOrder, OpAddendum{kind, e.also}});
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
    static const QStringList names = OpSchema::desktop().getForbidden();
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
