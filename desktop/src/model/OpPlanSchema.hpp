#pragma once
#include <QByteArray>
#include <QString>

#include <memory>

namespace stencil::core::opplan {
  class Schema;
}

// The desktop's seam onto core/opplan: opRegistry.json resolved for one surface, and a model reply
// walked into the one result document. JSON text in and out, for QJsonDocument.
namespace stencil::model {

  class OpPlanSchema {
   public:
    OpPlanSchema(const QByteArray& registryJson, const char* surface);
    ~OpPlanSchema();
    OpPlanSchema(const OpPlanSchema&) = delete;
    OpPlanSchema& operator=(const OpPlanSchema&) = delete;

    // "" when the registry resolved; else why not.
    QString error() const;
    // {surface, profile, entries, forbidden, hardFail, limits, defaultCustomLabel, registryBytes, …}
    QByteArray entriesJson() const;
    // A reply's UTF-8 bytes → {status, reply, actions, variants, ask, warnings, error}; empty for a
    // broken registry.
    QByteArray walk(const QByteArray& reply) const;

   private:
    std::unique_ptr<core::opplan::Schema> schema;
  };

}  // namespace stencil::model
