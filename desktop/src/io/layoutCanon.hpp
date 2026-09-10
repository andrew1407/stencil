#pragma once
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QString>
#include <QStringList>

// The layout export field list lives in the shared canon browser/js/config/
// layoutFields.json (qrc-embedded as :/config/layoutFields.json) — the same table the
// browser's buildLayoutPayload walks. fileStore::buildLayoutJson builds its values per
// field and emits them through here, so adding or dropping an exported field is a canon
// edit rather than a second hard-coded list. A key the canon doesn't declare still goes
// out (last), so a writer/canon mismatch reads as a diff, never as a dropped field.
namespace stencil::gui::layoutCanon {

  // Keys tagged with an `export` index, in that index's order. Read once; empty when
  // the qrc alias is missing.
  inline const QStringList& exportKeys() {
    static const QStringList keys = [] {
      QFile f(QStringLiteral(":/config/layoutFields.json"));
      QMap<int, QString> byOrder;
      if (f.open(QIODevice::ReadOnly))
        for (const QJsonValue& v : QJsonDocument::fromJson(f.readAll()).array()) {
          const QJsonObject o = v.toObject();
          if (o.contains(QStringLiteral("export")))
            byOrder[o.value(QStringLiteral("export")).toInt()] =
                o.value(QStringLiteral("key")).toString();
        }
      return QStringList(byOrder.values());
    }();
    return keys;
  }

  // The writer's per-field values (an absent key is an omitted field) as the envelope.
  inline QJsonObject emitExport(QMap<QString, QJsonValue> vals) {
    QJsonObject o;
    for (const QString& k : exportKeys())
      if (vals.contains(k)) o[k] = vals.take(k);
    for (auto it = vals.constBegin(); it != vals.constEnd(); ++it) o[it.key()] = it.value();
    return o;
  }

}
