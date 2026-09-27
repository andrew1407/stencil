// The model's reply to an executable plan (llm-contract §1): core/opplan walks it — fences, the first
// JSON object, the caps, the registry validation — and this maps its one result document onto the
// typed plan the executor runs. Split across opPlan*.cpp.
#include "opPlanParts.hpp"

#include "OpPlanSchema.hpp"

#include <climits>

namespace stencil::llm {

  using namespace opdetail;

  namespace {
    // A card's own note lands after its option's warnings: before a later option's dropped preview, or
    // the reply-tolerance note that always closes the list.
    QStringList warningTexts(const QJsonArray& warnings, const QVector<QPair<int, QString>>& askNotes) {
      QStringList out;
      int next = 0;
      const auto notesBefore = [&](int option) {
        while (next < askNotes.size() && askNotes[next].first < option) out << askNotes[next++].second;
      };
      for (const QJsonValue& v : warnings) {
        const QJsonObject w = v.toObject();
        const QString code = w.value(QLatin1String("code")).toString();
        if (code == QLatin1String("W_REPLY_OMITTED")) notesBefore(INT_MAX);
        else if (code == QLatin1String("W_PREVIEW_DROPPED")) notesBefore(w.value(QLatin1String("index")).toInt());
        out << w.value(QLatin1String("message")).toString();
      }
      notesBefore(INT_MAX);
      return out;
    }
  }  // namespace

  OpPlanResult parseOpPlan(const QString& text) {
    OpPlanResult r;
    const QJsonObject doc = QJsonDocument::fromJson(planSchema().walk(text.toUtf8())).object();
    const QString status = doc.value("status").toString();
    if (status == QLatin1String("chatOnly")) {
      // Chat-only turn: the raw text is the reply (not an error).
      r.ok = true;
      r.plan.chatOnly = true;
      r.plan.reply = doc.value("reply").toString();
      return r;
    }
    if (status != QLatin1String("valid")) {
      r.error = doc.isEmpty() ? planSchema().error() : doc.value("error").toObject().value("message").toString();
      return r;
    }
    OpPlan plan;
    plan.reply = doc.value("reply").toString();
    QVector<QPair<int, QString>> askNotes;
    QString e;
    if (!fillActions(doc.value("actions").toArray(), plan.actions, &e) ||
        !fillVariants(doc.value("variants").toArray(), plan.variants, &e) ||
        !fillAsk(doc.value("ask"), plan.ask, askNotes, &e)) {
      r.error = e;
      return r;
    }
    plan.warnings = warningTexts(doc.value("warnings").toArray(), askNotes);
    r.plan = std::move(plan);
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
    const int cap = planLimit(QStringLiteral("ask.answer"));
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
