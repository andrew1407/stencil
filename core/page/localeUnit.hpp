#pragma once
#include <string>

namespace stencil::core::localeUnit {

  // Same integer values as Qt's QLocale::MeasurementSystem; the GUI maps onto this and
  // never decides the cm/in policy itself.
  enum class MeasurementSystem {
    Metric = 0,      // QLocale::MetricSystem
    ImperialUS = 1,  // QLocale::ImperialUSSystem (== QLocale::ImperialSystem)
    ImperialUK = 2,  // QLocale::ImperialUKSystem
  };

  // "in" for US customary only; the UK is metric for length, so "cm".
  std::string defaultUnit(MeasurementSystem system);

}
