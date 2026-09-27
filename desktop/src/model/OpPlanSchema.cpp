#include "OpPlanSchema.hpp"

#include "planWalk.hpp"

namespace stencil::model {

  OpPlanSchema::OpPlanSchema(const QByteArray& registryJson, const char* surface)
      : schema(std::make_unique<core::opplan::Schema>()) {
    schema->load(std::string_view(registryJson.constData(), static_cast<std::size_t>(registryJson.size())),
                 surface, nullptr);
  }

  OpPlanSchema::~OpPlanSchema() = default;

  QString OpPlanSchema::error() const { return QString::fromStdString(schema->error); }

  QByteArray OpPlanSchema::entriesJson() const {
    return QByteArray::fromStdString(schema->entriesJson());
  }

  QByteArray OpPlanSchema::walk(const QByteArray& reply) const {
    if (!schema->error.empty()) return QByteArray();
    return QByteArray::fromStdString(
        core::opplan::walkPlan(*schema, std::string_view(reply.constData(), static_cast<std::size_t>(reply.size()))).text);
  }

}  // namespace stencil::model
