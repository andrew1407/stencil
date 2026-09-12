// WebAssembly ABI for the pure project rules in core/state/projectsStore.cpp.
//
// The registry itself is not a twin of the browser's store (the JS class is
// localStorage-backed; this one is in-memory), but the expiry arithmetic and the
// refresh presets are the same rule on both sides and must not drift. They read only
// scalars, so they cross as plain functions — no handle, no snapshot codec.
// Epoch milliseconds cross as doubles: exact well past 2^53, and no BigInt in the
// browser wrapper. Pinned by browser/tests/wasm-parity-projects.test.js.

#include "projectsStore.hpp"

#include <optional>
#include <string>

using namespace stencil::core;

namespace {

  // The rules read nothing but expiresAt, so a meta carrying it is the whole input.
  ProjectMeta withExpiry(double expiresAt) {
    ProjectMeta m;
    m.expiresAt = static_cast<long long>(expiresAt);
    return m;
  }

  const ProjectsStore& rules() {
    static const ProjectsStore store;
    return store;
  }

}

extern "C" {

  // Milliseconds for a refresh preset; unknown or empty is one week.
  double stencil_projects_periodMs(const char* period) {
    return static_cast<double>(ProjectsStore::periodMs(period ? period : ""));
  }

  double stencil_projects_addPeriod(double from, const char* period) {
    return static_cast<double>(
        ProjectsStore::addPeriod(static_cast<long long>(from), period ? period : ""));
  }

  // hasActiveId is 0 for the JS null active id.
  int stencil_projects_shouldPersist(int hasActiveId, int temporary) {
    std::optional<std::string> activeId;
    if (hasActiveId != 0) activeId = std::string();
    return ProjectsStore::shouldPersist(activeId, temporary != 0) ? 1 : 0;
  }

  // expiresAt of 0 is "keep forever" on both sides.
  int stencil_projects_isExpired(double expiresAt, double now) {
    return rules().isExpired(withExpiry(expiresAt), static_cast<long long>(now)) ? 1 : 0;
  }

  int stencil_projects_isExpiringSoon(double expiresAt, double now) {
    return rules().isExpiringSoon(withExpiry(expiresAt), static_cast<long long>(now)) ? 1 : 0;
  }

}
