// Native coverage for the project-rules ABI (core/wasmProjectsApi.cpp): the preset
// durations and the expiry predicates the browser store shares with this core.
#include "doctest.h"
#include "ProjectsStore.hpp"

extern "C" {
  double stencil_projects_periodMs(const char*);
  double stencil_projects_addPeriod(double, const char*);
  int stencil_projects_shouldPersist(int, int);
  int stencil_projects_isExpired(double, double);
  int stencil_projects_isExpiringSoon(double, double);
}

using stencil::core::ProjectsStore;

TEST_CASE("projects ABI: presets, and anything unknown falls back to a week") {
  CHECK(stencil_projects_periodMs("day") == static_cast<double>(ProjectsStore::DAY_MS));
  CHECK(stencil_projects_periodMs("week") == static_cast<double>(ProjectsStore::EXPIRY_MS));
  CHECK(stencil_projects_periodMs("year") == static_cast<double>(365 * ProjectsStore::DAY_MS));
  CHECK(stencil_projects_periodMs("") == static_cast<double>(ProjectsStore::EXPIRY_MS));
  CHECK(stencil_projects_periodMs(nullptr) == static_cast<double>(ProjectsStore::EXPIRY_MS));
  CHECK(stencil_projects_periodMs("banana") == static_cast<double>(ProjectsStore::EXPIRY_MS));
}

TEST_CASE("projects ABI: epoch milliseconds cross as exact doubles") {
  const double now = 1757000000000.0;   // 2025-09, well past 2^32
  CHECK(stencil_projects_addPeriod(now, "day") == now + static_cast<double>(ProjectsStore::DAY_MS));
  CHECK(stencil_projects_addPeriod(now, "3month") == now + static_cast<double>(90 * ProjectsStore::DAY_MS));
}

TEST_CASE("projects ABI: expiry predicates, with 0 meaning keep forever") {
  const double now = 1757000000000.0;
  CHECK(stencil_projects_isExpired(0, now) == 0);
  CHECK(stencil_projects_isExpired(now - 1, now) == 1);
  CHECK(stencil_projects_isExpired(now, now) == 0);            // strictly after
  CHECK(stencil_projects_isExpiringSoon(0, now) == 0);
  CHECK(stencil_projects_isExpiringSoon(now - 1, now) == 0);   // already expired
  CHECK(stencil_projects_isExpiringSoon(now + 1, now) == 1);
  const double day = static_cast<double>(ProjectsStore::WARN_MS);
  CHECK(stencil_projects_isExpiringSoon(now + day, now) == 1); // exactly at the window
  CHECK(stencil_projects_isExpiringSoon(now + day + 1, now) == 0);
}

TEST_CASE("projects ABI: persist only with an active, non-temporary project") {
  CHECK(stencil_projects_shouldPersist(1, 0) == 1);
  CHECK(stencil_projects_shouldPersist(1, 1) == 0);
  CHECK(stencil_projects_shouldPersist(0, 0) == 0);
  CHECK(stencil_projects_shouldPersist(0, 1) == 0);
}
