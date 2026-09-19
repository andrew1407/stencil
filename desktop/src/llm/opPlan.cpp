// The model's reply to an executable plan (llm-contract §1): strip the code fences, find the first
// JSON object in the prose, then hand it to the field fillers. Split across opPlan*.cpp.
#include "opPlanParts.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <cmath>

namespace stencil::llm {

  using namespace opdetail;

  namespace {
    // Remove Markdown code-fence points (``` with an optional language tag) so
    // a fenced JSON block parses like bare JSON.
    QString stripFences(QString text) {
      static const QRegularExpression fence(QStringLiteral("```[A-Za-z]*"));
      return text.remove(fence);
    }

    // First balanced { ... } substring that parses as a JSON OBJECT (brace counting skips string
    // literals). Chat text holding incidental balanced braces is skipped over, not failed.
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
    // The substitute must not overstate what happened: "Done." only when the plan actually carries
    // work - an empty plan says so, since a bare "Done." there reads as a success that never occurred.
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
