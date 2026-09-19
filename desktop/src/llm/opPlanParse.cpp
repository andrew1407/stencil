// Walking the model's reply into a plan: the actions array and the §11 ask card, each tolerant of a
// missing or off-shape entry (it becomes a warning, never a throw).
#include "opPlanParts.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <cmath>

namespace stencil::llm::opdetail {

    // params => fail the whole plan; a top-level-only op in a variant => *scopeDrop, caller drops it.
    bool parseActions(const QJsonValue& v, QVector<Action>& out, QStringList& warnings,
                      bool inVariant, QString* e, QString* scopeDrop) {
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
            if (a.op == OpKind::OPEN_URL)
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

    // §11 interactive replies (`ask`). The card's structure - keys, caps, 2..5 options, the image
    // reference's exactly-one-of url / projectId / scanIndex, http(s)-only urls - is the registry's.
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
          // Preview actions are rendered, never executed, so editor-settings ops are banned here as inside
          // variants. §1/§11.2: a misplaced one costs the PREVIEW, not the plan.
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
}  // namespace stencil::llm::opdetail
