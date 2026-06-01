#pragma once
#include <optional>
#include <string>
#include <vector>

// In-memory project registry + expiry logic. Port of the *pure* parts of
// browser/js/core/projectsStore.js: the registry model, one-week expiry sweep,
// id / default-name allocation, and ordering. Serialization and file I/O live in
// the GUI adapter (desktop/gui/fileStore) so this stays dependency-free and the
// expiry rule is identical across both apps.
namespace stencil::core {

  // Lightweight metadata for one saved project. The heavy payload (image bytes,
  // layout) is the adapter's concern; the core only reasons about metadata.
  struct ProjectMeta {
    std::string id;
    std::string name;
    long long createdAt = 0;   // epoch milliseconds
    long long updatedAt = 0;   // epoch milliseconds
    bool hasImage = false;
    int imageW = 0;
    int imageH = 0;
  };

  class ProjectsStore {
   public:
    // One week, matching EXPIRY_MS in the browser store.
    static constexpr long long EXPIRY_MS = 7LL * 24 * 60 * 60 * 1000;

    // Persist only when there is an active, non-temporary project to write to.
    static bool shouldPersist(const std::optional<std::string>& activeId,
                              bool temporary);

    // Replace the whole registry (e.g. after loading a file).
    void load(std::vector<ProjectMeta> registry);

    // All projects, most-recently-updated first.
    std::vector<ProjectMeta> list() const;

    // Metadata for `id`, or nullopt.
    std::optional<ProjectMeta> getMeta(const std::string& id) const;

    // Insert or replace `meta`, stamping updatedAt = now (and createdAt if unset).
    // Returns the stored metadata.
    ProjectMeta upsert(ProjectMeta meta, long long now);

    // Bump updatedAt without otherwise changing the entry. False if not found.
    bool touch(const std::string& id, long long now);

    // Remove one project. No-op if absent.
    void remove(const std::string& id);

    // Wipe every project.
    void clearAll();

    // ── expiry ──
    bool isExpired(const ProjectMeta& meta, long long now) const;
    std::optional<long long> expiresAt(const ProjectMeta& meta) const;

    // Remove every expired project; returns the removed ids.
    std::vector<std::string> sweepExpired(long long now);

    // ── allocation ──
    // "p_" + base36(now) + "_" + salt, mirroring the browser id shape. `salt`
    // supplies the randomness the JS version gets from Math.random().
    std::string createId(long long now, const std::string& salt) const;

    // "Untitled N", one past the highest existing Untitled index.
    std::string defaultName() const;

   private:
    std::vector<ProjectMeta> registry_;
  };

}
