#include "localeUnit.hpp"

namespace stencil::core::localeUnit {

  std::string defaultUnit(MeasurementSystem system) {
    return system == MeasurementSystem::ImperialUS ? "in" : "cm";
  }

}
