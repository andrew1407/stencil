// Core's result document becoming the typed plan: the normalized actions, the kept variants and the
// §11 card. Validation is core's; what is left here is the desktop's own field filling.
#include "opPlanParts.hpp"

namespace stencil::llm::opdetail {

  bool fillActions(const QJsonArray& list, QVector<Action>& out, QString* e) {
    for (const QJsonValue& v : list) {
      const QJsonObject n = v.toObject();
      Action a;
      if (!fillAction(n.value("op").toString(), n, a, e)) return false;
      out.push_back(std::move(a));
    }
    return true;
  }

  bool fillVariants(const QJsonArray& list, QVector<Variant>& out, QString* e) {
    for (const QJsonValue& v : list) {
      const QJsonObject vo = v.toObject();
      Variant var;
      var.label = vo.value("label").toString();  // absent → empty
      if (!fillActions(vo.value("actions").toArray(), var.actions, e)) return false;
      out.push_back(std::move(var));
    }
    return true;
  }

  // An option previews a render (`actions`) OR names an image; a page-scan image (§8) means nothing
  // in the editor, so that option stays pictureless with a note.
  bool fillAsk(const QJsonValue& card, AskCard& out, QVector<QPair<int, QString>>& notes, QString* e) {
    if (!card.isObject()) return true;  // no card is the norm
    const QJsonObject ask = card.toObject();
    out.question = ask.value("question").toString();
    out.multi = ask.value("mode").toString() == QLatin1String("multi");
    out.allowCustom = ask.value("allowCustom").toBool();
    out.customLabel = ask.value("customLabel").toString();
    int index = 0;
    for (const QJsonValue& ov : ask.value("options").toArray()) {
      ++index;
      const QJsonObject oo = ov.toObject();
      AskOption opt;
      opt.label = oo.value("label").toString();
      if (!fillActions(oo.value("actions").toArray(), opt.actions, e)) return false;
      if (present(oo, "image")) {
        const QJsonObject img = oo.value("image").toObject();
        if (present(img, "scanIndex"))
          notes.append({index, QStringLiteral("option %1 names a page-scan image, which this editor cannot show").arg(index)});
        else if (present(img, "projectId"))
          opt.projectId = img.value("projectId").toString();
        else
          opt.imageUrl = img.value("url").toString();
      }
      out.options.push_back(std::move(opt));
    }
    return true;
  }

}  // namespace stencil::llm::opdetail
