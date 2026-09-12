#include "localeUnit.hpp"

namespace stencil::core::localeUnit {

  std::string defaultUnit(MeasurementSystem system) {
    return system == MeasurementSystem::IMPERIAL_US ? "in" : "cm";
  }

}
