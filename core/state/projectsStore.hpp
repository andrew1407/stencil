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
    // Explicit expiration, epoch milliseconds. 0 == "keep forever" (never
    // expires). New/legacy projects are seeded with a real value by the
    // creation / migration code in the adapters; the store never invents one.
    long long expiresAt = 0;
    // Preset used by the Refresh button and the open-time auto-refresh:
    // one of day/week/fortnight/month/3month/6month/year. Empty == week.
    std::string refreshPeriod = "week";
    // When true, opening the project restamps expiresAt = openTime + period.
    bool autoRefresh = true;
    bool hasImage = false;
    int imageW = 0;
    int imageH = 0;
    // Provenance: the image/video's own URL (source) and the web page it was
    // pulled from (resource). Empty for plain local uploads; set by the
    // add-by-URL flow. Mirrors the browser project's source/resource fields.
    std::string source;
    std::string resource;
    // Optional per-project accent colour used to paint the project's NAME
    // wherever it appears. Normalised lower-case "#rrggbb", or empty = no
    // custom colour (fall back to the active theme accent). Mirrors the
    // browser project's `color` field.
    std::string color;
    // Optional search keywords (normalised: trimmed, non-empty, deduped). Mirrors the
    // browser project's `keywords` field + the server ProjectRecord.Keywords; used by the
    // keyword search + the CLI /keywords commands. Empty = none.
    std::vector<std::string> keywords;
    // Blank-image projects: `blank` marks a project whose background is a solid fill (created via
    // the Blank tab / stencil.blank), and `blankColor` is that fill ("#rrggbb"). Both are empty/
    // false for ordinary image projects. The fill can be recoloured after creation (the lines are
    // a separate vector overlay), which is why the colour is persisted. Mirrors the browser
    // project's blank/blankColor fields + the server ProjectRecord.
    bool blank = false;
    std::string blankColor;
  };

  class ProjectsStore {
   public:
    // Milliseconds in one day — the base unit every preset multiplies. Single
    // source of truth for the day length used across periodMs / EXPIRY / WARN.
    static constexpr long long DAY_MS = 24LL * 60 * 60 * 1000;
    // One week — the default refresh period, matching EXPIRY_MS in the browser
    // store. Also the ms length of the "week" preset (see periodMs).
    static constexpr long long EXPIRY_MS = 7 * DAY_MS;
    // Warn once a project is within a day of expiry (browser WARN_MS).
    static constexpr long long WARN_MS = DAY_MS;
    // The default refresh preset name.
    static constexpr const char* DEFAULT_PERIOD = "week";

    // Milliseconds for a refresh preset. Fixed durations (month=30d, year=365d,
    // …) so the C++ and JS ports stay identical with no calendar library.
    // Unknown / empty falls back to one week. Mirrors browser projectsStore.js.
    static long long periodMs(const std::string& period);
    // from + periodMs(period). The custom-calendar pick sets an exact date
    // instead; only the presets use this fixed-duration arithmetic.
    static long long addPeriod(long long from, const std::string& period);

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

    // Set a project's expiration fields exactly (no snap, no updatedAt bump).
    // expiresAt == 0 means "keep forever". This is what the expiration modal
    // calls. False if the id is not found.
    bool setExpiration(const std::string& id, long long expiresAt,
                       const std::string& refreshPeriod, bool autoRefresh);

    // Remove one project. No-op if absent.
    void remove(const std::string& id);

    void clearAll();

    // ── expiry ──
    // All keyed on the stored expiresAt; expiresAt == 0 == "keep forever".
    bool isExpired(const ProjectMeta& meta, long long now) const;
    std::optional<long long> expiresAt(const ProjectMeta& meta) const;

    // Not yet expired but due within WARN_MS — the cue for a warning colour.
    // Already-expired projects return false (they get the stronger treatment).
    bool isExpiringSoon(const ProjectMeta& meta, long long now) const;

    // Remove every expired project; returns the removed ids.
    std::vector<std::string> sweepExpired(long long now);

    // ── allocation ──
    // "p_" + base36(now) + "_" + salt, mirroring the browser id shape. `salt`
    // supplies the randomness the JS version gets from Math.random().
    std::string createId(long long now, const std::string& salt) const;

    // "Untitled N", one past the highest existing Untitled index.
    std::string defaultName() const;

    // ── name validation (mirrors the browser store) ──
    // True when another project (id != exceptId) already uses `name` (trimmed,
    // case-insensitive). Drives the "no duplicate names" guard on rename.
    bool nameExists(const std::string& name, const std::string& exceptId = {}) const;

    // Result of validating a proposed project name.
    struct NameCheck {
      bool ok = false;
      std::string reason;  // human-readable rejection reason when !ok
    };
    // Validate a proposed name → { ok, reason }. Rejects empty / too-long (>80
    // chars) / duplicate names with a reason; otherwise ok. `exceptId` is the
    // project being renamed (so its own current name isn't a self-collision).
    NameCheck validateName(const std::string& name, const std::string& exceptId = {}) const;

   private:
    // Locate a project by id in the registry (end() iterator if absent). Both
    // overloads so const and mutating callers share one linear scan.
    std::vector<ProjectMeta>::iterator findById(const std::string& id);
    std::vector<ProjectMeta>::const_iterator findById(const std::string& id) const;

    std::vector<ProjectMeta> registry_;
  };

}
