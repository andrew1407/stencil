// WebAssembly ABI for the pure project rules of core/state/projectsStore.cpp (the
// registry itself is not a twin of the browser's localStorage store). Epoch ms cross
// as doubles — exact past 2^53, no BigInt. Pinned by browser/tests/wasm-parity-projects.

#include "projectsStore.hpp"

#include <optional>
#include <string>

using namespace stencil::core;

namespace {

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

  int stencil_projects_isExpired(double expiresAt, double now) {
    return rules().isExpired(withExpiry(expiresAt), static_cast<long long>(now)) ? 1 : 0;
  }

  int stencil_projects_isExpiringSoon(double expiresAt, double now) {
    return rules().isExpiringSoon(withExpiry(expiresAt), static_cast<long long>(now)) ? 1 : 0;
  }

}
