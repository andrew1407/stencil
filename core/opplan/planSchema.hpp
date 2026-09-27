#pragma once
#include "jsonReader.hpp"
#include "jsonValue.hpp"

#include <string>
#include <string_view>
#include <vector>

// opRegistry.json resolved for one surface: its op entries, forbidden names and policy, surface
// rules and caps. Twin of createSchema in browser/js/llm/plan/opSchema.js. The host passes the
// registry text in — core reads no file. Never throws: a bad registry sets `error`.
namespace stencil::core::opplan {

  // One op this surface registers, resolved (surfaceKeys / bulletVariants / surfaceFlags).
  struct Entry {
    std::string name;
    const json::Value* raw = nullptr;
    const json::Value* keys = nullptr;
    json::Value flags;
    const json::Value* bullet = nullptr;
    const json::Value* addendum = nullptr;
    bool topLevelOnly = false;
    bool settings = false;
  };

  // One `surfaceRules` row for this surface.
  struct SurfaceRule {
    std::string op;
    std::string rule;
    const json::Value* row = nullptr;
  };

  class Schema {
   public:
    Schema() = default;
    Schema(const Schema&) = delete;
    Schema& operator=(const Schema&) = delete;

    // `capabilities` null = every capability wired; else a comma-separated list gating `requires`.
    void load(std::string_view registryJson, std::string_view surface, const char* capabilities);

    std::string error;
    std::string surface;
    std::string profile;
    json::Value registry;
    std::vector<Entry> entries;
    std::vector<std::string> forbidden;
    bool hardFail = false;
    std::vector<SurfaceRule> surfaceRules;
    json::Caps caps;
    std::string defaultCustomLabel;
    std::size_t registryBytes = 0;
    unsigned long long registryFnv1a64 = 0;

    const Entry* find(std::string_view op) const;
    bool isForbidden(std::string_view op) const;
    // A cap: the number itself, or a dotted name into `limits` ("MAX_ACTIONS", "ask.label").
    double limit(const json::Value& v) const;
    double limitNamed(std::string_view name) const;
    // The wording a grammar goes by in messages (`regexes.describe`).
    std::string describe(std::string_view grammar) const;
    const json::Value& askSchema() const;
    const json::Value& envelope(std::string_view slot) const;

    // The resolved surface as JSON, for a host's registry and prompt assembly; memoised.
    const std::string& entriesJson() const;

   private:
    const json::Value* limitValue(std::string_view name) const;
    bool validate();
    mutable std::string entriesText;
  };

  // The registry caps core parses the registry itself under; tests pin them to limits.json.
  inline constexpr json::Caps REGISTRY_CAPS{2097152, 64, 262144};

}  // namespace stencil::core::opplan
