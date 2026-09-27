#include "planSchema.hpp"

#include "jsonWriter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace stencil::core::opplan {

  using json::Value;

  namespace {
    const Value* at(const Value* v, std::string_view key) { return v ? v->get(key) : nullptr; }

    bool listHas(const Value* list, std::string_view s) {
      if (!list || !list->isArray()) return false;
      for (const Value& x : list->items)
        if (x.isString() && x.text == s) return true;
      return false;
    }

    // `map[surface] ?? map[profile]`.
    const Value* forSurface(const Value* map, const std::string& surface, const std::string& profile) {
      const Value* v = at(map, surface);
      if (v && !v->isNull()) return v;
      v = at(map, profile);
      return v && !v->isNull() ? v : nullptr;
    }

    std::size_t sizeOf(const Value* v) {
      return v && v->isNumber() && v->number > 0 ? static_cast<std::size_t>(v->number) : 0;
    }

    unsigned long long fnv1a64(std::string_view bytes) {
      unsigned long long h = 14695981039346656037ull;
      for (char c : bytes) h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ull;
      return h;
    }
  }  // namespace

  void Schema::load(std::string_view text, std::string_view surf, const char* capabilities) {
    registryBytes = text.size();
    registryFnv1a64 = fnv1a64(text);
    json::ReadResult read = json::readJson(text, REGISTRY_CAPS);
    if (read.status != json::ReadStatus::OK || !read.value.isObject()) {
      error = "opRegistry: the registry is not a JSON object within the core's caps";
      return;
    }
    registry = std::move(read.value);
    surface = std::string(surf);
    const Value* prof = at(at(registry.get("$meta"), "surfaceProfiles"), surface);
    if (!prof || !prof->isString()) {
      error = "opRegistry: unknown surface \"" + surface + "\"";
      return;
    }
    profile = prof->text;
    const Value* order = at(at(registry.get("profiles"), profile), "ops");
    const Value* ops = registry.get("ops");
    if (!order || !order->isArray() || !ops || !ops->isArray()) {
      error = "opRegistry: no op list for profile \"" + profile + "\"";
      return;
    }
    std::vector<std::string> wired;
    for (const char* c = capabilities; c && *c;) {
      const char* end = c;
      while (*end && *end != ',') ++end;
      wired.emplace_back(c, end);
      c = *end ? end + 1 : end;
    }
    const auto rank = [&](const std::string& name) -> long {
      for (std::size_t i = 0; i < order->items.size(); ++i)
        if (order->items[i].isString() && order->items[i].text == name) return static_cast<long>(i);
      return -1;
    };
    for (const Value& e : ops->items) {
      const Value* name = e.get("name");
      if (!name || !name->isString() || !listHas(e.get("profiles"), profile)) continue;
      const Value* only = e.get("surfaces");
      if (only && !listHas(only, surface)) continue;
      bool ok = true;
      if (capabilities) {
        if (const Value* req = e.get("requires"))
          for (const Value& r : req->items)
            ok = ok && r.isString() && std::find(wired.begin(), wired.end(), r.text) != wired.end();
      }
      if (!ok) continue;
      Entry out;
      out.name = name->text;
      out.raw = &e;
      const Value* own = at(e.get("surfaceKeys"), surface);
      out.keys = own && !own->isNull() ? own : e.get("keys");
      out.flags = Value::object();
      const Value* merged[2] = {e.get("flags"), forSurface(e.get("surfaceFlags"), surface, profile)};
      for (const Value* f : merged)
        if (f && f->isObject())
          for (std::size_t i = 0; i < f->keys.size(); ++i) out.flags.set(f->keys[i], f->items[i]);
      out.bullet = e.get("bullet");
      if (const Value* variant = forSurface(e.get("bulletVariants"), surface, profile)) {
        if (variant->isString()) out.bullet = variant;
        else out.addendum = variant->get("addendum");
      }
      const auto flag = [&](const char* k) { const Value* f = out.flags.get(k); return f && f->isBool() && f->flag; };
      out.topLevelOnly = flag("topLevelOnly");
      out.settings = flag("editorSetting") || flag("consoleSetting");
      entries.push_back(std::move(out));
    }
    std::stable_sort(entries.begin(), entries.end(),
                     [&](const Entry& a, const Entry& b) { return rank(a.name) < rank(b.name); });
    const Value* forbid = registry.get("forbidden");
    if (const Value* list = at(at(forbid, "perSurface"), surface))
      for (const Value& x : list->items)
        if (x.isString()) forbidden.push_back(x.text);
    hardFail = listHas(at(forbid, "hardFail"), surface);
    if (const Value* rules = at(registry.get("surfaceRules"), surface); rules && rules->isArray())
      for (const Value& r : rules->items) {
        const Value* op = r.get("op");
        const Value* rule = r.get("rule");
        surfaceRules.push_back({op && op->isString() ? op->text : "", rule && rule->isString() ? rule->text : "", &r});
      }
    if (const Value* row = at(registry.get("limits"), "json"))
      caps = json::Caps{sizeOf(row->get("MAX_BYTES")), sizeOf(row->get("MAX_DEPTH")), sizeOf(row->get("MAX_NODES"))};
    const Value* label = at(registry.get("ask"), "defaultCustomLabel");
    defaultCustomLabel = label && label->isString() ? label->text : "";
    validate();
  }

  const Entry* Schema::find(std::string_view op) const {
    for (const Entry& e : entries)
      if (e.name == op) return &e;
    return nullptr;
  }

  bool Schema::isForbidden(std::string_view op) const {
    return std::find(forbidden.begin(), forbidden.end(), op) != forbidden.end();
  }

  const Value* Schema::limitValue(std::string_view name) const {
    const Value* v = registry.get("limits");
    while (v && !name.empty()) {
      const std::size_t dot = name.find('.');
      v = v->get(name.substr(0, dot));
      name = dot == std::string_view::npos ? std::string_view() : name.substr(dot + 1);
    }
    return v && v->isNumber() ? v : nullptr;
  }

  double Schema::limitNamed(std::string_view name) const {
    const Value* v = limitValue(name);
    return v ? v->number : NAN;
  }

  double Schema::limit(const Value& v) const {
    if (v.isNumber()) return v.number;
    return v.isString() ? limitNamed(v.text) : NAN;
  }

  std::string Schema::describe(std::string_view grammar) const {
    const Value* d = at(at(registry.get("regexes"), "describe"), grammar);
    return d && d->isString() && !d->text.empty() ? d->text : std::string(grammar);
  }

  const Value& Schema::askSchema() const {
    static const Value none;
    const Value* s = at(registry.get("ask"), "schema");
    return s ? *s : none;
  }

  const Value& Schema::envelope(std::string_view slot) const {
    static const Value none;
    const Value* s = at(registry.get("envelope"), slot);
    return s ? *s : none;
  }

}  // namespace stencil::core::opplan
