#pragma once
// Private seam between the opPlan TUs: the small presence/whitelist checks the field filling and the
// parsing both use, and the two fillers themselves. Not part of the public opPlan.hpp surface.
#include "opPlan.hpp"

#include "opRegistry.hpp"
#include "OpSchema.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace stencil::llm {

  namespace opdetail {

    inline bool err(QString* out, const QString& msg) {
      if (out) *out = msg;
      return false;
    }

    inline bool present(const QJsonObject& o, const char* key) {
      const QJsonValue v = o.value(QLatin1String(key));
      return !v.isUndefined() && !v.isNull();
    }

    // desktop extras: checks the registry does not carry (see each op's
    //    `divergence`), run AFTER the generic check

    // The read scope: only the formats this app itself opens, decided by extension so the model can
    // never hand us an arbitrary file to slurp (never a directory). Mirrors the cli's understoodPath.
    inline bool isOpenableFile(const QString& path) {
      static const QStringList EXTS = {
          QStringLiteral("png"),  QStringLiteral("jpg"),  QStringLiteral("jpeg"),
          QStringLiteral("bmp"),  QStringLiteral("tga"),  QStringLiteral("gif"),
          QStringLiteral("webp"), QStringLiteral("mp4"),  QStringLiteral("mov"),
          QStringLiteral("m4v"),  QStringLiteral("avi"),  QStringLiteral("mkv"),
          QStringLiteral("webm"), QStringLiteral("json"), QStringLiteral("stencil")};
      const int dot = path.lastIndexOf(QLatin1Char('.'));
      if (dot < 0) return false;
      const int slash = std::max(path.lastIndexOf(QLatin1Char('/')), path.lastIndexOf(QLatin1Char('\\')));
      if (dot < slash) return false;  // the dot is in a directory name
      return EXTS.contains(path.mid(dot + 1).toLower());
    }
    bool fillLayout(const QJsonObject& n, Action& a, QString* e);
    bool fillAction(const QString& op, const QJsonObject& n, Action& a, QString* e);

    bool parseActions(const QJsonValue& v, QVector<Action>& out, QStringList& warnings,
                        bool inVariant, QString* e, QString* scopeDrop = nullptr);
    bool parseAsk(const QJsonValue& value, AskCard& out, QStringList& warnings, QString* e);

  }  // namespace opdetail

}  // namespace stencil::llm
