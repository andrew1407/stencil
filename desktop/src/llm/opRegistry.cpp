#include "opRegistry.hpp"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QtGlobal>

// The §4 prose core rides in app.qrc (the shared canon
// browser/js/config/llm/systemPrompt.json). A pre-main caller can reach it
// before the resource's own global initializer ran, so force registration on
// first read — same guard as theme.cpp. Global scope: Q_INIT_RESOURCE declares
// the generated init function.
static void ensureAppResources() { Q_INIT_RESOURCE(app); }

// The desktop op registry (llm-contract.md §13). Every prompt bullet below is
// the §4/§10 text VERBATIM — the prose core loads from the shared canon asset,
// the ops section is assembled from the table, so the prompt can never promise
// an op this surface cannot run and adding/removing an op is one entry here.
namespace stencil::llm {

  namespace {

  // §4 prose core, parsed once from the qrc canon. "head" carries its trailing
  // newline and "tail" its leading blank line, so assembly is plain
  // head + bullets + tail. Empty strings on a broken alias — the configCanon
  // pins and the llmClient byte-stability test fail fast on that.
  struct PromptProse { QString head; QString tail; };

  const PromptProse& promptProse() {
    static const PromptProse prose = [] {
      ensureAppResources();
      PromptProse p;
      QFile f(QStringLiteral(":/config/llm/systemPrompt.json"));
      if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        p.head = o.value(QStringLiteral("head")).toString();
        p.tail = o.value(QStringLiteral("tail")).toString();
      }
      return p;
    }();
    return prose;
  }

  constexpr char kBCrop[] =
      R"__(- {"op":"crop","spec":{"x1":"10%","x2":"-10%","aspect":"3:4"}} — move edges inward;
  tokens are numbers with optional unit % / px / cm / in; a leading "-" measures from the
  opposite side. Include only the edges you want to move. For a target aspect ratio add
  "aspect":"W:H" INSIDE "spec", never beside it (portrait "3:4", album/landscape "4:3",
  square "1:1") — the editor cuts the resolved crop to that exact ratio about its centre,
  so NEVER derive ratio tokens yourself; combine it with edge tokens when a specific
  region should be kept.)__";

  constexpr char kBRotate[] =
      R"__(- {"op":"rotate","dir":"left"|"right","times":1..3} — quarter turns only.)__";

  constexpr char kBFilter[] =
      R"__(- {"op":"filter","mode":"none"|"bw"|"sepia"|"invert"|"contour"|"custom","tint":"#rrggbb"}
  — "custom" is a duotone tint and requires "tint"; "contour" is edge detection.)__";

  constexpr char kBLayout[] =
      R"__(- {"op":"layout","lines":[{"points":[{"x":0,"y":0},...],"color":"#FFFF00","thickness":2,
  "pointSize":4,"style":"solid"|"dashed"|"dotted","locked":false,"fillColor":"transparent"}]}
  — draw annotation polylines in image-pixel coordinates. When asked to extract lines,
  shapes, or structure from an attached image, answer with this op. An empty "lines"
  array REMOVES every drawn line — that is what "clear/remove the lines" means.)__";

  constexpr char kBFormula[] =
      R"__(- {"op":"formula","axis":"x"|"y","expr":"x*2+10"} — coordinate transform; single variable
  matching the axis; operators + - * / ** and parentheses only. An empty "expr" clears
  that axis; {"op":"formula","enabled":false} switches formulas OFF entirely.)__";

  constexpr char kBPage[] =
      R"__(- {"op":"page","format":"a4"} — ISO page formats a0–a10, b0–b10, c0–c10 — or a custom
  size: {"op":"page","width":20,"height":30} in centimetres (one form or the other).)__";

  constexpr char kBBlank[] =
      R"__(- {"op":"blank","color":"#ffffff","format":"a4"} — create a blank page; explicit
  centimetre dims ride as "width"/"height" instead of "format".)__";

  constexpr char kBUndoRedo[] =
      R"__(- {"op":"undo","steps":1} / {"op":"redo","steps":1} — step this surface's edit history.
  "Undo that" means {"op":"undo"}; steps count history entries, which can be finer
  than one request.)__";

  constexpr char kBFrame[] =
      R"__(- {"op":"frame","index":0} or {"op":"frame","indices":[0,30,60]} — pick video frame(s);
  only valid when the current input is a video.)__";

  constexpr char kBImage[] =
      R"__(- {"op":"image","index":1} — switch the working image to the Nth image attached to THIS
  message (1-based, in attachment order); coordinates in later actions are in THAT
  image's pixel frame. Only valid when the user attached images. Use it to edit several
  attached images in one plan, giving each image its OWN actions.)__";

  constexpr char kBSave[] =
      R"__(- {"op":"save","name":"portrait 1","path":"~/Downloads"} — save the current image with its
  drawn lines. "path" is optional and may be a folder or a file name (".stencil" saves the
  whole project, an image extension saves the picture); with no path it becomes a project in
  the editor. ONLY a path the user themselves wrote in this conversation — never invent,
  complete or rewrite one. When the user asks to process several images and keep the results,
  finish each image's actions with a "save" before switching to the next: image 1, its edits,
  save, image 2, its edits, save, …)__";

  constexpr char kBTheme[] =
      R"__(- {"op":"theme","mode":"light"|"dark"} — switch the editor between light and dark
  ONLY; "mode" takes no other value. A COLOUR ("make the theme cyan") is the accent
  op below, never this one.)__";

  constexpr char kBAccent[] =
      R"__(- {"op":"accent","color":"#7c3aed"} — set the editor accent colour. "color" must be
  a #rrggbb hex, so translate colour names yourself (cyan = "#00ffff").)__";

  constexpr char kBLineStyle[] =
      R"__(- {"op":"lineStyle","color":"#00ff00","thickness":3,"pointSize":6,"style":"dashed"} —
  change the DEFAULT style for new lines (any subset of fields).)__";

  constexpr char kBUnits[] =
      R"__(- {"op":"units","value":"cm"|"in"} — display units.)__";

  constexpr char kBView[] =
      R"__(- {"op":"view","points":true,"lines":false} — show or hide points and lines.)__";

  constexpr char kBClear[] =
      R"__(- {"op":"clear"} — REMOVE the working image and its lines, leaving the editor empty.
  This is what "remove/delete/clear the image" means. Never answer that with
  {"op":"blank"}: a blank REPLACES the picture with a white page, which is not a
  removal. Takes no fields.)__";

  constexpr char kBOpenUrl[] =
      R"__(- {"op":"openUrl","url":"https://…","incognito":false} — load an image (or video
  frame) from a URL into the editor; "incognito": true loads it into THIS editor
  switched to incognito (nothing is saved), never a second tab or window, so the
  rest of your plan keeps acting on it. ONLY a URL the user themselves wrote in
  this conversation — never introduce, complete, or rewrite one.)__";

  constexpr char kBOpenFile[] =
      R"__(- {"op":"openFile","path":"~/Pictures/portrait.png"} — load a LOCAL file the user named
  into the editor: an image or video, a layout ".json" (drawn onto the current picture), or a
  ".stencil" project. ONLY a path the user themselves wrote in this conversation — never
  invent, complete, guess or list one, and never a directory.)__";

  constexpr char kBConnectDisconnect[] =
      R"__(- {"op":"connect","server":"..."} / {"op":"disconnect","server":"..."} — manage the
  user's collaboration-server connections. Only a server the user has already saved
  may be named — never invent or suggest a new address. These editor ops are not
  image edits and cannot appear inside "variants".)__";

  constexpr char kBCopy[] =
      R"__(- {"op":"copy"} — copy the current rendered image to the system clipboard. This IS
  what "copy the result / copy to clipboard" means; never answer that it cannot be
  done. Takes no fields.)__";

  constexpr char kBRemoveProject[] =
      R"__(- {"op":"removeProject","name":"portrait 1"} — remove ONE saved local project by its
  name; the app asks the user to confirm before anything is deleted.)__";

  constexpr char kBClearProjects[] =
      R"__(- {"op":"clearProjects"} — remove EVERY saved local project. This IS what "clear/
  delete my projects" means; the app asks the user to confirm first. Server-stored
  projects are never touched from chat. Takes no fields.)__";

  constexpr char kBCompare[] =
      R"__(- {"op":"compare","mode":"none"|"original"|"vertical"|"horizontal","split":0.5} — the
  comparison view: the original beside/over the edit ("vertical" = side-by-side split).
  View-only; the exported image is unchanged.)__";

  constexpr char kBZoom[] =
      R"__(- {"op":"zoom","percent":150} or {"op":"zoom","fit":true} — zoom the USER'S VIEW (or
  fit to the window). This never changes the picture — cropping is the crop op.)__";

  constexpr char kBRenameProject[] =
      R"__(- {"op":"renameProject","name":"…"} — rename the active saved project.)__";

  constexpr char kBProjectColor[] =
      R"__(- {"op":"projectColor","color":"#ec4899"} — the project's name colour ("" = theme).)__";

  constexpr char kBBlankColor[] =
      R"__(- {"op":"blankColor","color":"#dbeafe"} — recolour a BLANK project's background,
  KEEPING the drawn lines. "Recolour/change the background" means THIS, never a new
  {"op":"blank"} (that replaces the page and destroys the lines).)__";

  constexpr char kBOpenProject[] =
      R"__(- {"op":"openProject","name":"…"} — open a saved local project into the editor (the
  app confirms first when unsaved work would be replaced).)__";

  constexpr char kBIncognito[] =
      R"__(- {"op":"incognito","on":true} — edit without saving; only togglable on a blank editor.)__";

  constexpr char kBClearChat[] =
      R"__(- {"op":"clearChat"} — clear THIS conversation's history; the app asks the user to
  confirm first, and the clear happens after this plan's other actions finish. This IS
  what "clear the chat / conversation / history" means; never answer that it cannot be
  done. Takes no fields.)__";

  constexpr char kBAddRemoveProject[] =
      R"__(- "removeProject" also accepts {"op":"removeProject","current":true} — remove the
  project that is open right now (confirmed in-app).)__";

  constexpr char kBAddCopy[] =
      R"__(- "copy" also accepts {"op":"copy","what":"layout"} — the layout JSON instead of the
  image.)__";

  constexpr char kBAddAccentPreset[] =
      R"__(- "accent" also accepts {"op":"accent","preset":"green"} — a named preset persists and
  syncs; use a preset when the user names a colour that has one.)__";

  constexpr char kBAddLineStyle[] =
      R"__(- "lineStyle" also carries "pointColor" ("" = follow the stroke), "drawMode"
  ("line"|"rect") and "fillColor" for the defaults of NEW lines.)__";
  }  // namespace

  // Emission order = final prompt order: the §2 core ops, then the §10 editor
  // block spliced between the frame and image bullets (undo/redo and
  // connect/disconnect share one bullet each — shared pointers, emitted once).
  const QVector<OpDescriptor>& opRegistry() {
    static const QVector<OpDescriptor> table = {
        // kind, name, bullet, editorSettings, topLevelOnly, history, capability
        {OpKind::Crop, "crop", kBCrop, false, false, false, CapNone},
        {OpKind::Rotate, "rotate", kBRotate, false, false, false, CapNone},
        {OpKind::Filter, "filter", kBFilter, false, false, false, CapNone},
        {OpKind::Layout, "layout", kBLayout, false, false, false, CapNone},
        {OpKind::Formula, "formula", kBFormula, false, false, false, CapNone},
        {OpKind::Page, "page", kBPage, false, false, false, CapNone},
        {OpKind::Blank, "blank", kBBlank, false, false, false, CapNone},
        {OpKind::Undo, "undo", kBUndoRedo, false, true, true, CapNone},
        {OpKind::Redo, "redo", kBUndoRedo, false, true, true, CapNone},
        {OpKind::Frame, "frame", kBFrame, false, false, false, CapVideo},
        {OpKind::Theme, "theme", kBTheme, true, true, false, CapNone},
        {OpKind::Accent, "accent", kBAccent, true, true, false, CapNone},
        {OpKind::LineStyle, "lineStyle", kBLineStyle, true, true, false, CapNone},
        {OpKind::Units, "units", kBUnits, true, true, false, CapNone},
        {OpKind::View, "view", kBView, true, true, false, CapNone},
        {OpKind::Clear, "clear", kBClear, true, true, false, CapNone},
        {OpKind::OpenUrl, "openUrl", kBOpenUrl, true, true, false, CapNone},
        {OpKind::OpenFile, "openFile", kBOpenFile, true, true, false, CapFilesystem},
        {OpKind::Connect, "connect", kBConnectDisconnect, true, true, false, CapServers},
        {OpKind::Disconnect, "disconnect", kBConnectDisconnect, true, true, false, CapServers},
        {OpKind::Copy, "copy", kBCopy, true, true, false, CapClipboard},
        {OpKind::RemoveProject, "removeProject", kBRemoveProject, true, true, false, CapNone},
        {OpKind::ClearProjects, "clearProjects", kBClearProjects, true, true, false, CapNone},
        {OpKind::Compare, "compare", kBCompare, true, true, false, CapNone},
        {OpKind::Zoom, "zoom", kBZoom, true, true, false, CapNone},
        {OpKind::RenameProject, "renameProject", kBRenameProject, true, true, false, CapNone},
        {OpKind::ProjectColor, "projectColor", kBProjectColor, true, true, false, CapNone},
        {OpKind::BlankColor, "blankColor", kBBlankColor, true, true, false, CapNone},
        {OpKind::OpenProject, "openProject", kBOpenProject, true, true, false, CapNone},
        {OpKind::Incognito, "incognito", kBIncognito, true, true, false, CapNone},
        {OpKind::ClearChat, "clearChat", kBClearChat, true, true, false, CapNone},
        {OpKind::Image, "image", kBImage, false, true, false, CapNone},
        {OpKind::Save, "save", kBSave, false, true, false, CapNone},
    };
    return table;
  }

  // §10 "also accepts" widenings — closing the editor block, each dropped
  // with its op when the op's capability is missing.
  const QVector<OpAddendum>& opAddenda() {
    static const QVector<OpAddendum> addenda = {
        {OpKind::RemoveProject, kBAddRemoveProject},
        {OpKind::Copy, kBAddCopy},
        {OpKind::Accent, kBAddAccentPreset},
        {OpKind::LineStyle, kBAddLineStyle},
    };
    return addenda;
  }

  QString opName(OpKind kind) {
    for (const OpDescriptor& e : opRegistry())
      if (e.kind == kind) return QString::fromUtf8(e.name);
    return QString();
  }

  QStringList forbiddenOps() {
    // §13 categories, by name. Never registered; the registry test and the
    // executor reject are the two enforcement teeth.
    static const QStringList names = {
        // llm/provider configuration
        "llm", "llmProvider", "llmModel", "llmBaseUrl", "llmApiKey",
        "setProvider", "setModel", "setEndpoint", "setApiKey",
        // clipboard reads
        "paste", "readClipboard", "clipboardRead",
        // hotkey rebinding
        "hotkey", "setHotkey", "rebindHotkey", "keybinding", "rebindKey",
        // session/window end
        "quit", "exit", "closeWindow", "endSession", "logout",
        // chat persistence/consent toggles
        "chatPersistence", "persistChat", "chatConsent", "chatOptIn",
        // server-side destruction beyond what §10 grants
        "deleteServerProject", "clearServerProjects", "removeServerFile",
        "deleteRemote",
    };
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
    QVector<const char*> emitted;   // shared-bullet dedupe (undo/redo, connect/disconnect)
    QVector<OpKind> included;
    int lastEditorAt = -1;
    auto push = [&](const char* text) -> bool {
      const QString b = QString::fromUtf8(text);
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
      const QString b = QString::fromUtf8(ad.bullet);
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
    return promptProse().head + assembleOpsSection(caps) + promptProse().tail;
  }

  const QString& assembledSystemPrompt() {
    static const QString prompt = assembleSystemPrompt(CapAllDesktop);
    return prompt;
  }

}  // namespace stencil::llm
