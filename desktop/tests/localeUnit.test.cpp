#include "core/localeUnit.hpp"
#include "doctest.h"

using namespace stencil::core::localeUnit;

TEST_CASE("defaultUnit maps measurement systems to cm/in") {
  // Only US customary uses inches for everyday length.
  CHECK(defaultUnit(MeasurementSystem::ImperialUS) == "in");
  // Metric and the UK (metric for length) default to centimetres.
  CHECK(defaultUnit(MeasurementSystem::Metric) == "cm");
  CHECK(defaultUnit(MeasurementSystem::ImperialUK) == "cm");
}

TEST_CASE("defaultUnit enum values mirror QLocale::MeasurementSystem") {
  // The GUI casts QLocale's enum onto these; guard the contract.
  CHECK(static_cast<int>(MeasurementSystem::Metric) == 0);
  CHECK(static_cast<int>(MeasurementSystem::ImperialUS) == 1);
  CHECK(static_cast<int>(MeasurementSystem::ImperialUK) == 2);
}
