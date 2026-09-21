#pragma once
#include "ProjectMeta.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// In-memory project registry + expiry rules. Port of the *pure* parts of
// browser/js/core/project/store/projectsStore.js; serialization and file I/O live in the adapters.
namespace stencil::core {

  class ProjectsStore {
   public:
    static constexpr long long DAY_MS = 24LL * 60 * 60 * 1000;
    // The default refresh period and the "week" preset (browser EXPIRY_MS).
    static constexpr long long EXPIRY_MS = 7 * DAY_MS;
    // Browser WARN_MS.
    static constexpr long long WARN_MS = DAY_MS;
    static constexpr const char* DEFAULT_PERIOD = "week";

    // Fixed durations (month=30d, year=365d) so the C++ and JS ports agree with no
    // calendar library; unknown / empty is one week. Mirrors browser PERIOD_MS.
    static long long periodMs(const std::string& period);
    static long long addPeriod(long long from, const std::string& period);

    static bool shouldPersist(const std::optional<std::string>& activeId,
                              bool temporary);

    void load(std::vector<ProjectMeta> registry);

    std::vector<ProjectMeta> list() const;

    // list() without the copies; valid until the next mutation.
    std::vector<const ProjectMeta*> listRefs() const;

    // Insertion order, unsorted, and still holding the id-less rows list() drops.
    const std::vector<ProjectMeta>& getRegistry() const { return registry; }

    std::optional<ProjectMeta> getMeta(const std::string& id) const;

    // getMeta without the copy; valid until the next mutation.
    const ProjectMeta* find(const std::string& id) const;

    // Stamps updatedAt = now (and createdAt if unset); returns the stored row.
    ProjectMeta upsert(ProjectMeta meta, long long now);

    // upsert without the copies; the reference is valid until the next mutation.
    const ProjectMeta& upsertMoved(ProjectMeta&& meta, long long now);

    bool touch(const std::string& id, long long now);

    // Exact set, no snap and no updatedAt bump; expiresAt == 0 is "keep forever".
    bool setExpiration(const std::string& id, long long expiresAt,
                       const std::string& refreshPeriod, bool autoRefresh);

    void remove(const std::string& id);

    void clearAll();

    // Expiry: all keyed on the stored expiresAt, where 0 is "keep forever".
    bool isExpired(const ProjectMeta& meta, long long now) const;
    std::optional<long long> expiresAt(const ProjectMeta& meta) const;

    // Due within WARN_MS; already-expired projects answer false.
    bool isExpiringSoon(const ProjectMeta& meta, long long now) const;

    std::vector<std::string> sweepExpired(long long now);

    // "p" + base36(now) + "_" + salt; `salt` stands in for the browser's Math.random().
    std::string createId(long long now, const std::string& salt) const;

    std::string defaultName() const;

    // Trimmed, case-insensitive, ignoring `exceptId` (the project being renamed).
    bool nameExists(const std::string& name, const std::string& exceptId = {}) const;

    struct NameCheck {
      bool ok = false;
      std::string reason;  // human-readable rejection reason when !ok
    };
    // Characters. Callers that pre-trim a name clamp to this, never to a literal 80.
    static constexpr std::size_t MAX_NAME_LENGTH = 80;

    // Rejects empty / too-long / duplicate names with a reason.
    NameCheck validateName(const std::string& name, const std::string& exceptId = {}) const;

   private:
    std::vector<ProjectMeta>::iterator findById(const std::string& id);
    std::vector<ProjectMeta>::const_iterator findById(const std::string& id) const;

    // First occurrence of an id wins, like the linear scan it replaces.
    void reindex();

    std::vector<ProjectMeta> registry;
    std::unordered_map<std::string, std::size_t> index;  // id -> position in registry
  };

}
