#pragma once
#include <string>

namespace stencil::core::localeUnit {

  // Mirrors Qt's QLocale::MeasurementSystem (same integer values). Kept here so
  // the cm/in policy lives in the STL-only, unit-tested core; the GUI maps the
  // QLocale enum onto this and never decides the policy itself.
  enum class MeasurementSystem {
    Metric = 0,      // QLocale::MetricSystem
    ImperialUS = 1,  // QLocale::ImperialUSSystem (== QLocale::ImperialSystem)
    ImperialUK = 2,  // QLocale::ImperialUKSystem
  };

  // Default display unit for the given measurement system. Only US customary
  // uses inches for everyday length; metric and the UK (which is metric for
  // length) default to centimetres. Returns "in" or "cm".
  std::string defaultUnit(MeasurementSystem system);

}
