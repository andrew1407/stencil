#include "opPlan.hpp"

#include "colorNames.hpp"
#include "opRegistry.hpp"
#include "opSchema.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>

#include <cmath>

namespace stencil::llm {

  namespace {

    // ── extraction ───────────────────────────────────────────────────────────

    // Remove Markdown code-fence points (``` with an optional language tag) so
    // a fenced JSON block parses like bare JSON.
    QString stripFences(QString text) {
      static const QRegularExpression fence(QStringLiteral("```[A-Za-z]*"));
      return text.remove(fence);
    }

    // First balanced { … } substring that parses as a JSON OBJECT (brace
    // counting skips string literals, so braces inside "reply" don't confuse
    // it). Chat text containing incidental balanced braces that aren't JSON is
    // skipped over rather than failing the turn.
    bool extractFirstObject(const QString& text, QJsonObject& out) {
      const int n = text.size();
      for (int start = text.indexOf(QLatin1Char('{')); start >= 0;
           start = text.indexOf(QLatin1Char('{'), start + 1)) {
        int depth = 0;
        bool inStr = false, esc = false;
        for (int i = start; i < n; ++i) {
          const QChar c = text.at(i);
          if (inStr) {
            if (esc) esc = false;
            else if (c == QLatin1Char('\\')) esc = true;
            else if (c == QLatin1Char('"')) inStr = false;
            continue;
          }
          if (c == QLatin1Char('"')) {
            inStr = true;
          } else if (c == QLatin1Char('{')) {
            ++depth;
          } else if (c == QLatin1Char('}')) {
            if (--depth == 0) {
              const QJsonDocument doc =
                  QJsonDocument::fromJson(text.mid(start, i - start + 1).toUtf8());
              if (doc.isObject()) {
                out = doc.object();
                return true;
              }
              break;  // balanced but not JSON — try the next '{'
            }
          }
        }
      }
      return false;
    }

    bool err(QString* out, const QString& msg) {
      if (out) *out = msg;
      return false;
    }

    bool present(const QJsonObject& o, const char* key) {
      const QJsonValue v = o.value(QLatin1String(key));
      return !v.isUndefined() && !v.isNull();
    }

    // ── desktop extras: checks the registry does not carry (see each op's
    //    `divergence`), run AFTER the generic check ────────────────────────────

    // A colour NAME must be one the core recognizes (fixture 137
    // knownDivergence.desktop); "#rrggbb" already passed the grammar.
    bool isKnownColor(const QString& t) {
      return t.startsWith(QLatin1Char('#')) || core::parseColor(t.toStdString()).has_value();
    }

    // The read scope: only the formats this app itself opens, decided by extension so the
    // model can never hand us an arbitrary file to slurp (a recorded desktop+cli
    // divergence; never a directory). Mirrors the cli's understoodPath.
    bool isOpenableFile(const QString& path) {
      static const QStringList kExts = {
          QStringLiteral("png"),  QStringLiteral("jpg"),  QStringLiteral("jpeg"),
          QStringLiteral("bmp"),  QStringLiteral("tga"),  QStringLiteral("gif"),
          QStringLiteral("webp"), QStringLiteral("mp4"),  QStringLiteral("mov"),
          QStringLiteral("m4v"),  QStringLiteral("avi"),  QStringLiteral("mkv"),
          QStringLiteral("webm"), QStringLiteral("json"), QStringLiteral("stencil")};
      const int dot = path.lastIndexOf(QLatin1Char('.'));
      if (dot < 0) return false;
      const int slash = std::max(path.lastIndexOf(QLatin1Char('/')), path.lastIndexOf(QLatin1Char('\\')));
      if (dot < slash) return false;  // the dot is in a directory name
      return kExts.contains(path.mid(dot + 1).toLower());
    }

    // ── struct filling ────────────────────────────────────────────────────────
    // The validated + normalized action (declared keys, registry defaults, trims) →
    // the typed Action. Types, enums, ranges, grammars and presence rules are the
    // generic check's; only the desktop extras above can still fail here.
    bool fillLayout(const QJsonObject& n, Action& a, QString* e) {
      for (const QJsonValue& lv : n.value("lines").toArray()) {
        const QJsonObject o = lv.toObject();
        core::Line line;
        for (const QJsonValue& pv : o.value("points").toArray()) {
          const QJsonObject po = pv.toObject();
          line.points.push_back({po.value("x").toDouble(), po.value("y").toDouble()});
        }
        if (line.points.empty())
          return err(e, QStringLiteral("Invalid layout action: a line needs a non-empty \"points\" array"));
        if (present(o, "color")) line.color = o.value("color").toString().toStdString();
        if (present(o, "thickness")) line.thickness = o.value("thickness").toDouble();
        if (present(o, "pointSize")) line.pointSize = o.value("pointSize").toDouble();
        if (present(o, "style")) line.style = o.value("style").toString().toStdString();
        if (present(o, "locked")) line.locked = o.value("locked").toBool();
        if (present(o, "fillColor")) line.fillColor = o.value("fillColor").toString().toStdString();
        a.lines.push_back(std::move(line));
      }
      return true;
    }

    bool fillAction(const QString& op, const QJsonObject& n, Action& a, QString* e) {
      if (!opKindFor(op, &a.op))
        return err(e, QStringLiteral("Invalid %1 action: no desktop executor").arg(op));
      const auto str = [&](const char* k) { return n.value(QLatin1String(k)).toString(); };
      const auto num = [&](const char* k) { return n.value(QLatin1String(k)).toDouble(); };
      const auto integer = [&](const char* k) { return n.value(QLatin1String(k)).toInt(); };
      const auto flag = [&](const char* k) { return n.value(QLatin1String(k)).toBool(); };
      switch (a.op) {
        case OpKind::Crop: {
          const QJsonObject spec = n.value("spec").toObject();
          a.x1 = spec.value("x1").toString();
          a.x2 = spec.value("x2").toString();
          a.y1 = spec.value("y1").toString();
          a.y2 = spec.value("y2").toString();
          a.aspect = spec.value("aspect").toString();
          break;
        }
        case OpKind::Rotate:
          a.rotateLeft = str("dir") == QLatin1String("left");
          a.times = integer("times");
          break;
        case OpKind::Filter:
          a.mode = str("mode");
          a.tint = str("tint");
          break;
        case OpKind::Layout:
          return fillLayout(n, a, e);
        case OpKind::Formula:
          if (present(n, "enabled")) {
            a.formulaEnabled = flag("enabled") ? 1 : 0;
          } else {
            a.axis = str("axis").at(0);
            a.expr = str("expr").trimmed();   // blank = clear that axis
          }
          break;
        case OpKind::Page:
          a.format = str("format");
          a.widthCm = num("width");
          a.heightCm = num("height");
          break;
        case OpKind::Blank:
          a.color = str("color");
          if (!isKnownColor(a.color))
            return err(e, QStringLiteral("Invalid blank action: \"color\" must be #rrggbb or a CSS colour name"));
          a.format = str("format");
          a.widthCm = num("width");
          a.heightCm = num("height");
          break;
        case OpKind::Undo:
        case OpKind::Redo:
          a.steps = integer("steps");
          break;
        case OpKind::Frame:
          if (present(n, "index")) a.indices.push_back(integer("index"));
          for (const QJsonValue& v : n.value("indices").toArray()) a.indices.push_back(v.toInt());
          break;
        case OpKind::Theme:
          a.mode = str("mode");
          break;
        case OpKind::Accent:
          a.color = str("color");
          a.preset = str("preset");
          break;
        case OpKind::LineStyle:
          a.color = str("color");
          a.thickness = integer("thickness");
          a.pointSize = integer("pointSize");
          a.style = str("style");
          // "" = follow the stroke — meaningful, so a presence flag rides along.
          a.pointColorSet = present(n, "pointColor");
          a.pointColor = str("pointColor");
          a.drawMode = str("drawMode");
          a.fillColor = str("fillColor");
          break;
        case OpKind::Units:
          a.value = str("value");
          break;
        case OpKind::View:
          if (present(n, "points")) a.viewPoints = flag("points") ? 1 : 0;
          if (present(n, "lines")) a.viewLines = flag("lines") ? 1 : 0;
          break;
        case OpKind::Clear:
        case OpKind::ClearChat:
          break;
        case OpKind::Copy:
          a.what = str("what");
          break;
        case OpKind::OpenUrl:
          a.url = str("url");
          a.incognito = flag("incognito");
          break;
        case OpKind::OpenFile:
          // A LOCAL file (the registry rejects URL schemes) in a format this app
          // opens; whether the USER wrote it is the executor's echo guard.
          a.path = str("path");
          if (!isOpenableFile(a.path))
            return err(e, QStringLiteral("Invalid openFile action: \"%1\" is not an image, video, "
                                         ".json layout or .stencil project")
                              .arg(a.path));
          break;
        case OpKind::Connect:
        case OpKind::Disconnect:
          a.server = str("server").trimmed();
          break;
        case OpKind::RemoveProject:
          a.name = str("name");
          a.current = flag("current");
          break;
        case OpKind::ClearProjects:
          a.current = flag("keepCurrent");   // the shared "the open one" presence flag
          break;
        case OpKind::Compare:
          a.mode = str("mode");
          a.split = num("split");
          break;
        case OpKind::Zoom:
          a.fit = flag("fit");
          a.percent = static_cast<int>(std::lround(num("percent")));
          break;
        case OpKind::RenameProject:
          a.name = str("name");
          break;
        case OpKind::ProjectColor:
          a.color = str("color");   // "" = the explicit clear
          break;
        case OpKind::BlankColor:
          a.color = str("color");
          if (!isKnownColor(a.color))
            return err(e, QStringLiteral("Invalid blankColor action: \"color\" must be #rrggbb or a CSS colour name"));
          break;
        case OpKind::OpenProject:
          a.name = str("name");
          a.current = flag("last");   // "the latest one" rides removeProject's flag
          break;
        case OpKind::Incognito:
          a.incognito = flag("on");
          break;
        case OpKind::ChatPanel:
          if (present(n, "open")) a.chatOpen = flag("open") ? 1 : 0;
          a.dock = str("dock");
          break;
        case OpKind::Dialog:
          a.dialog = str("name");
          a.current = flag("close");   // "close what is open" rides the shared flag
          break;
        case OpKind::Image:
          a.index = integer("index");
          break;
        case OpKind::Save:
          a.name = str("name");
          a.path = str("path");   // trimmed by the registry; "" = no destination
          break;
      }
      return true;
    }

    // One action list (top-level or a variant's). Unknown op ⇒ skip + warning
    // (a §13 forbidden name lands here too — the executor refuses it); known op
    // with bad params ⇒ fail (the caller fails the whole plan); a top-level-only
    // op inside a variant/preview ⇒ *scopeDrop = why, and the CALLER drops that
    // variant (or that option's preview) with a warning instead of failing the
    // plan (§1's one exception).
    bool parseActions(const QJsonValue& v, QVector<Action>& out, QStringList& warnings,
                      bool inVariant, QString* e, QString* scopeDrop = nullptr) {
      if (v.isUndefined() || v.isNull()) return true;  // absent = empty
      const OpSchema& schema = OpSchema::desktop();
      if (!schema.checkEnvelope(v, QStringLiteral("actions"), e)) return false;
      for (const QJsonValue& av : v.toArray()) {
        const QJsonObject o = av.toObject();
        if (!o.value("op").isString())
          return err(e, QStringLiteral("Invalid plan: action has no \"op\""));
        const QString op = o.value("op").toString();
        const OpEntry* entry = schema.entry(op);
        if (!entry) {
          // Forward compatibility: an unknown op is dropped with a warning.
          warnings << QStringLiteral("Skipped unknown op \"%1\".").arg(op);
          continue;
        }
        QJsonObject validated;
        if (!schema.validateAction(o, *entry, &validated, e)) return false;
        Action a;
        if (!fillAction(op, schema.normalize(validated, *entry), a, e)) return false;
        if (inVariant && isTopLevelOnlyOp(a.op)) {
          QString why;
          if (isHistoryOp(a.op)) {
            // §2 undo/redo: their own wording — a sandboxed variant/preview
            // render writes history-invisible state.
            why = QStringLiteral(
                      "\"%1\" is a top-level action only — a sandboxed variant/preview "
                      "has no edit history")
                      .arg(op);
          } else if (!isEditorSettingsOp(a.op)) {   // §2.1 image/save
            why = QStringLiteral("\"%1\" is a top-level action only (§2.1)").arg(op);
          } else {
            why = QStringLiteral("\"%1\" is an editor-settings op, not an image edit").arg(op);
            if (a.op == OpKind::OpenUrl)
              why += QStringLiteral(" — open the URL as a top-level action; picking images "
                                    "off a web page is the browser extension assistant's job");
          }
          // §1: costs this variant/preview its place, never the whole plan.
          if (scopeDrop) { *scopeDrop = why; return false; }
          return err(e, why);
        }
        out.push_back(std::move(a));
      }
      return true;
    }

    // ── §11 interactive replies (`ask`) ──
    // The card's structure (keys, caps, 2..5 options, the image reference's exactly-one-of
    // url / projectId / scanIndex, http(s)-only urls) is the registry's ask schema; a card
    // nobody can answer fails the whole plan. Only the preview actions need this module.
    bool parseAsk(const QJsonValue& value, AskCard& out, QStringList& warnings, QString* e) {
      if (value.isUndefined() || value.isNull()) return true;   // no card is the norm
      const OpSchema& schema = OpSchema::desktop();
      if (!schema.validateAsk(value, e)) return false;
      const QJsonObject ask = schema.normalizeAsk(value.toObject());
      out.question = ask.value("question").toString();
      out.multi = ask.value("mode").toString() == QLatin1String("multi");
      out.allowCustom = ask.value("allowCustom").toBool();
      out.customLabel = ask.value("customLabel").toString();
      if (out.customLabel.isEmpty()) out.customLabel = schema.defaultCustomLabel();
      int index = 0;
      for (const QJsonValue& ov : ask.value("options").toArray()) {
        ++index;
        const QJsonObject oo = ov.toObject();
        AskOption opt;
        opt.label = oo.value("label").toString();
        if (present(oo, "actions")) {
          // Preview actions are rendered, never executed — editor-settings ops make no sense
          // here, so they are parsed under the same rule that bans them inside variants.
          // §1/§11.2: a misplaced one costs the PREVIEW, not the plan — the option stays,
          // pictureless, and the preview's own warnings go with the render it never gets.
          QStringList previewWarnings;
          QString scopeDrop;
          if (!parseActions(oo.value("actions"), opt.actions, previewWarnings,
                            /*inVariant=*/true, e, &scopeDrop)) {
            if (scopeDrop.isEmpty()) return false;
            opt.actions.clear();
            warnings << QStringLiteral(
                            "Dropped the preview for option %1 \"%2\" — %3; put it in the "
                            "plan's top-level actions. The option is still offered.")
                            .arg(index)
                            .arg(opt.label, scopeDrop);
          } else {
            warnings += previewWarnings;
          }
        }
        if (present(oo, "image")) {
          const QJsonObject img = oo.value("image").toObject();
          if (present(img, "scanIndex")) {
            // The extension's reference (§8): meaningless in the editor, so the option stays pictureless.
            warnings << QStringLiteral("option %1 names a page-scan image, which this editor cannot show").arg(index);
          } else if (present(img, "projectId")) {
            opt.projectId = img.value("projectId").toString();
          } else {
            opt.imageUrl = img.value("url").toString();
          }
        }
        out.options.push_back(std::move(opt));
      }
      return true;
    }

  }  // namespace

  OpPlanResult parseOpPlan(const QString& text) {
    OpPlanResult r;
    QJsonObject obj;
    if (!extractFirstObject(stripFences(text), obj)) {
      // Chat-only turn: the raw text is the reply (not an error).
      r.ok = true;
      r.plan.chatOnly = true;
      r.plan.reply = text.trimmed();
      return r;
    }
    const QJsonValue reply = obj.value("reply");
    // §1 reply tolerance: models routinely omit the reply while planning valid
    // actions — substitute rather than lose the plan to a missing pleasantry.
    const bool replyOmitted = !reply.isString() || reply.toString().trimmed().isEmpty();
    if (!replyOmitted) r.plan.reply = reply.toString();
    QString e;
    if (!parseActions(obj.value("actions"), r.plan.actions, r.plan.warnings,
                      /*inVariant=*/false, &e)) {
      r.plan = {};
      r.error = e;
      return r;
    }
    const QJsonValue vars = obj.value("variants");
    if (!vars.isUndefined() && !vars.isNull()) {
      // The envelope: ≤ 8 objects, each a string label + ≤ 16 action objects.
      if (!OpSchema::desktop().checkEnvelope(vars, QStringLiteral("variants"), &e)) {
        r.plan = {};
        r.error = e;
        return r;
      }
      int vIndex = 0;
      for (const QJsonValue& vv : vars.toArray()) {
        ++vIndex;
        const QJsonObject vo = vv.toObject();
        Variant var;
        var.label = vo.value("label").toString();  // absent → empty
        // §1: a top-level-only op in here drops THIS variant with a warning; the
        // rest of the plan runs. Its own warnings go with it.
        QStringList varWarnings;
        QString scopeDrop;
        if (!parseActions(vo.value("actions"), var.actions, varWarnings,
                          /*inVariant=*/true, &e, &scopeDrop)) {
          if (scopeDrop.isEmpty()) {
            r.plan = {};
            r.error = e;
            return r;
          }
          const QString who = var.label.trimmed().isEmpty()
                                  ? QStringLiteral("variant %1").arg(vIndex)
                                  : QStringLiteral("variant \"%1\"").arg(var.label.trimmed());
          r.plan.warnings << QStringLiteral(
                                 "Dropped %1 — %2; put it in the plan's top-level actions.")
                                 .arg(who, scopeDrop);
          continue;
        }
        r.plan.warnings += varWarnings;
        r.plan.variants.push_back(std::move(var));
      }
    }
    if (!parseAsk(obj.value("ask"), r.plan.ask, r.plan.warnings, &e)) {
      r.plan = {};
      r.error = e;
      return r;
    }
    // The substitute must not overstate what happened: "Done." only when the
    // plan actually carries work — an empty plan says so, since a bare "Done."
    // there reads as a success that never occurred.
    if (replyOmitted) {
      if (!r.plan.actions.isEmpty() || !r.plan.variants.isEmpty() ||
          !r.plan.ask.options.isEmpty()) {
        r.plan.reply = QStringLiteral("Done.");
        r.plan.warnings << QStringLiteral("The model omitted its reply — the plan still ran");
      } else {
        r.plan.reply =
            QStringLiteral("The model returned an empty plan — nothing was changed.");
      }
    }
    r.ok = true;
    return r;
  }

  QString askAnswerText(const QStringList& pickedLabels, const QString& custom) {
    const QString typed = custom.trimmed();
    QString answer;
    if (!typed.isEmpty()) {
      answer = typed;
    } else {
      QStringList kept;
      for (const QString& l : pickedLabels) {
        if (!l.trimmed().isEmpty()) kept << l.trimmed();
      }
      answer = kept.join(QStringLiteral(", "));
    }
    const int cap = OpSchema::desktop().limit(QStringLiteral("ask.answer"));
    return cap >= 0 && answer.size() > cap ? answer.left(cap) : answer;
  }

  QString sanitizeLabel(const QString& label) {
    QString out;
    for (const QChar c : label) {
      if (c.isLetterOrNumber() || c.isSpace() || c == QLatin1Char('-') ||
          c == QLatin1Char('_'))
        out.append(c);
    }
    // Collapse the runs left behind by dropped symbols, then bound the length.
    return out.simplified().left(40);
  }

}  // namespace stencil::llm
