#pragma once
#include <string>
#include <vector>

// Metadata for one saved project — the value type projectsStore and every adapter
// exchange. The heavy payload (image bytes, layout) is the adapter's concern.
// Mirrors the browser project object built by browser/js/core/projectMeta.js.
namespace stencil::core {

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
    // Cached real-world length of all drawn line segments, in centimetres, computed at
    // save time (page size + image dims + points are all live then). 0 = none / legacy.
    // Display-only; converted to the active unit for the projects-list tooltip. Mirrors
    // the browser project's `lineLengthCm` field. Not synced to the server.
    double lineLengthCm = 0.0;
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
    // Optional free-text description shown/edited in the projects list. Empty = none.
    // Mirrors the browser project's `description` field + the server ProjectRecord.
    std::string description;
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
    // Provenance: opened from a portable .stencil project file (mirrors the browser project's
    // meta.fromFile). Drives the projects list's bronze outline / .stencil badge; a plain local
    // project leaves it false. Persisted by the GUI adapter (desktop/fileStore).
    bool fromFile = false;
  };

}
