#include "opRegistry.hpp"
#include "opSchema.hpp"
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

// The desktop op registry (llm-contract.md §13): the shared opRegistry.json
// (opSchema) supplies every bullet, "also accepts" line, flag and forbidden
// name; this table adds the OpKind and the desktop capability each op needs.
// The prose core loads from the shared canon asset and the ops section is
// assembled from the table, so the prompt can never promise an op this
// surface cannot run and adding/removing an op is one row here.
namespace stencil::llm {

  // Any string field of the §4 prompt canon, parsed once from the qrc asset. "head"
  // carries its trailing newline and "tail" its leading blank line, so assembly is plain
  // head + bullets + tail; the contextSuffix* templates keep their Qt %1/%2 placeholders.
  // Empty on a broken alias — the configCanon pins and the llmClient byte-stability test
  // fail fast on that.
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
        {OpKind::Crop, "crop", CapNone},
        {OpKind::Rotate, "rotate", CapNone},
        {OpKind::Filter, "filter", CapNone},
        {OpKind::Layout, "layout", CapNone},
        {OpKind::Formula, "formula", CapNone},
        {OpKind::Page, "page", CapNone},
        {OpKind::Blank, "blank", CapNone},
        {OpKind::Undo, "undo", CapNone},
        {OpKind::Redo, "redo", CapNone},
        {OpKind::Frame, "frame", CapVideo},
        {OpKind::Theme, "theme", CapNone},
        {OpKind::Accent, "accent", CapNone},
        {OpKind::LineStyle, "lineStyle", CapNone},
        {OpKind::Units, "units", CapNone},
        {OpKind::View, "view", CapNone},
        {OpKind::Clear, "clear", CapNone},
        {OpKind::OpenUrl, "openUrl", CapNone},
        {OpKind::OpenFile, "openFile", CapFilesystem},
        {OpKind::Connect, "connect", CapServers},
        {OpKind::Disconnect, "disconnect", CapServers},
        {OpKind::Copy, "copy", CapClipboard},
        {OpKind::RemoveProject, "removeProject", CapNone},
        {OpKind::ClearProjects, "clearProjects", CapNone},
        {OpKind::Compare, "compare", CapNone},
        {OpKind::Zoom, "zoom", CapNone},
        {OpKind::RenameProject, "renameProject", CapNone},
        {OpKind::ProjectColor, "projectColor", CapNone},
        {OpKind::BlankColor, "blankColor", CapNone},
        {OpKind::OpenProject, "openProject", CapNone},
        {OpKind::Incognito, "incognito", CapNone},
        {OpKind::ChatPanel, "chatPanel", CapNone},
        {OpKind::Dialog, "dialog", CapNone},
        {OpKind::ClearChat, "clearChat", CapNone},
        {OpKind::Image, "image", CapNone},
        {OpKind::Save, "save", CapNone},
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
          d.history = r.kind == OpKind::Undo || r.kind == OpKind::Redo;
        }
        out.append(d);
      }
      return out;
    }();
    return table;
  }

  // §10 "also accepts" widenings — the registry's `also` lines in alsoOrder,
  // closing the editor block; each dropped with its op when the op's capability
  // is missing.
  const QVector<OpAddendum>& opAddenda() {
    static const QVector<OpAddendum> addenda = [] {
      QVector<QPair<int, OpAddendum>> ordered;
      for (const OpEntry& e : OpSchema::desktop().entries()) {
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
    static const QStringList names = OpSchema::desktop().forbidden();
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

  bool bulletLeaksSecrets(const QString& bullet) {
    // §13 censor patterns: api keys, bearer tokens, endpoint-setting
    // instructions. A match fails assembly loudly — never leaks into the prompt.
    static const QVector<QRegularExpression> patterns = {
        QRegularExpression("api[\\s_-]?key", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("\\bbearer\\b", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("access[\\s_-]?token", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("auth(orization)?[\\s_-]?token", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("\\bendpoint\\b", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("base[\\s_-]?url", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("\\bsecret\\b", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression("\\bpassword\\b", QRegularExpression::CaseInsensitiveOption),
    };
    for (const auto& re : patterns)
      if (re.match(bullet).hasMatch()) return true;
    return false;
  }

  QString assembleOpsBullets(const QVector<OpDescriptor>& entries,
                             const QVector<OpAddendum>& addenda, unsigned caps,
                             QString* censorError) {
    QStringList bullets;
    QStringList emitted;   // shared-bullet dedupe (undo/redo, connect/disconnect)
    QVector<OpKind> included;
    int lastEditorAt = -1;
    auto push = [&](const QString& b) -> bool {
      if (bulletLeaksSecrets(b)) {
        // §13 censor: fail loudly at assembly, drop the bullet.
        if (censorError && censorError->isEmpty())
          *censorError = QStringLiteral("op registry bullet matches a sensitive pattern");
        Q_ASSERT(censorError != nullptr);  // production callers must observe the failure
        return false;
      }
      bullets.append(b);
      return true;
    };
    for (const OpDescriptor& e : entries) {
      if ((caps & e.capability) != e.capability) continue;  // §13 capability truth
      included.append(e.kind);
      if (emitted.contains(e.bullet)) continue;
      emitted.append(e.bullet);
      if (push(e.bullet) && e.editorSettings) lastEditorAt = bullets.size() - 1;
    }
    // Addenda close the editor block (right after its last bullet).
    QStringList extra;
    for (const OpAddendum& ad : addenda) {
      if (!included.contains(ad.kind)) continue;
      const QString& b = ad.bullet;
      if (bulletLeaksSecrets(b)) {
        if (censorError && censorError->isEmpty())
          *censorError = QStringLiteral("op registry bullet matches a sensitive pattern");
        Q_ASSERT(censorError != nullptr);
        continue;
      }
      extra.append(b);
    }
    if (!extra.isEmpty() && lastEditorAt >= 0)
      for (int i = 0; i < extra.size(); ++i) bullets.insert(lastEditorAt + 1 + i, extra.at(i));
    return bullets.join(QLatin1Char('\n'));
  }

  QString assembleOpsSection(unsigned caps) {
    QString censor;
    const QString s = assembleOpsBullets(opRegistry(), opAddenda(), caps, &censor);
    Q_ASSERT(censor.isEmpty());
    return s;
  }

  QString assembleEditorOpsBlock(unsigned caps) {
    QVector<OpDescriptor> editor;
    for (const OpDescriptor& e : opRegistry())
      if (e.editorSettings) editor.append(e);
    QString censor;
    const QString s = assembleOpsBullets(editor, opAddenda(), caps, &censor);
    Q_ASSERT(censor.isEmpty());
    return s;
  }

  QString assembleSystemPrompt(unsigned caps) {
    return promptText(QStringLiteral("head")) + assembleOpsSection(caps)
           + promptText(QStringLiteral("tail"));
  }

  const QString& assembledSystemPrompt() {
    static const QString prompt = assembleSystemPrompt(CapAllDesktop);
    return prompt;
  }

}  // namespace stencil::llm
