#pragma once
// Private seam between the opPlan TUs: core's result document mapped onto the typed plan — the
// per-op field filling, the variants and the ask card.
#include "opPlan.hpp"

#include "opRegistry.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

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

    // Core's normalized actions / variants / §11 card → typed; `notes` gets the card's own notes.
    bool fillActions(const QJsonArray& list, QVector<Action>& out, QString* e);
    bool fillVariants(const QJsonArray& list, QVector<Variant>& out, QString* e);
    bool fillAsk(const QJsonValue& card, AskCard& out, QVector<QPair<int, QString>>& notes, QString* e);

  }  // namespace opdetail

}  // namespace stencil::llm
