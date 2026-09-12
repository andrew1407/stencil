#pragma once
#include <string>
#include <vector>

// Metadata for one saved project, field for field the browser project object of
// browser/js/core/projectMeta.js (and the server ProjectRecord). Payloads are the adapter's.
namespace stencil::core {

  struct ProjectMeta {
    std::string id;
    std::string name;
    long long createdAt = 0;   // epoch milliseconds
    long long updatedAt = 0;   // epoch milliseconds
    // Epoch ms; 0 == "keep forever". The adapters seed it, the store never invents one.
    long long expiresAt = 0;
    // day/week/fortnight/month/3month/6month/year; empty == week.
    std::string refreshPeriod = "week";
    // Opening the project restamps expiresAt = openTime + period.
    bool autoRefresh = true;
    bool hasImage = false;
    int imageW = 0;
    int imageH = 0;
    // Centimetres, cached at save time; 0 = none / legacy. Display-only, not synced.
    double lineLengthCm = 0.0;
    // Provenance of the add-by-URL flow: the image's own URL and the page it came from.
    std::string source;
    std::string resource;
    // Accent for the project's NAME: lower-case "#rrggbb", or empty for the theme accent.
    std::string color;
    std::string description;
    // Normalised: trimmed, non-empty, deduped.
    std::vector<std::string> keywords;
    // A solid-fill background ("#rrggbb"); persisted because the fill can be recoloured
    // after creation (the lines are a separate overlay).
    bool blank = false;
    std::string blankColor;
    // Opened from a portable .stencil file; drives the list's bronze outline / badge.
    bool fromFile = false;
  };

}
