#include "planGrammars.hpp"
#include "planSchema.hpp"

#include "jsonWriter.hpp"

#include <cmath>
#include <cstdio>

// The registry proven once, when the schema loads, so no check at plan time can meet a type,
// grammar, cap or rule it does not know; and the resolved surface written out for hosts.
namespace stencil::core::opplan {

  using json::Value;

  namespace {
    constexpr std::string_view TYPES[] = {"string", "integer", "number", "boolean", "object", "array"};
    constexpr std::string_view SURFACE_RULES[] = {"anyOf", "knownColor", "pathExtension"};

    bool strings(const Value* v) {
      if (!v || !v->isArray()) return false;
      for (const Value& x : v->items)
        if (!x.isString()) return false;
      return true;
    }
  }  // namespace

  bool Schema::validate() {
    std::string why;
    const auto grammar = [&](const Value* name) {
      if (name && name->isString() && grammarNamed(name->text)) return true;
      why = "opRegistry: unknown grammar";
      return false;
    };
    const auto cap = [&](const Value* v) {
      if (!v || v->isNull() || !std::isnan(limit(*v))) return true;
      why = "opRegistry: unknown limit \"" + (v->isString() ? v->text : std::string("?")) + "\"";
      return false;
    };
    // Registry nesting is bounded by REGISTRY_CAPS, so this recursion is too.
    const auto specOf = [&](const auto& self, const Value& s) -> bool {
      const Value* type = s.get("type");
      bool known = false;
      for (std::string_view t : TYPES) known = known || (type && type->isString() && type->text == t);
      if (!s.isObject() || !known) {
        why = "opRegistry: unknown type";
        return false;
      }
      if (!cap(s.get("maxChars")) || !cap(s.get("minItems")) || !cap(s.get("maxItems"))) return false;
      if (const Value* r = s.get("regex")) {
        if (r->isArray()) {
          for (const Value& g : r->items)
            if (!grammar(&g)) return false;
        } else if (!grammar(r)) {
          return false;
        }
      }
      if (s.get("regexNot") && !grammar(s.get("regexNot"))) return false;
      if (const Value* by = s.get("regexBy")) {
        const Value* map = by->get("map");
        if (!map || !map->isObject() || !by->get("key")) { why = "opRegistry: bad regexBy"; return false; }
        for (const Value& g : map->items)
          if (!grammar(&g)) return false;
      }
      if (const Value* items = s.get("items"); items && !self(self, *items)) return false;
      if (const Value* fields = s.get("fields")) {
        if (!fields->isObject()) { why = "opRegistry: fields must be an object"; return false; }
        for (const Value& f : fields->items)
          if (!self(self, f)) return false;
      }
      return true;
    };
    const auto spec = [&](const Value& s) { return specOf(specOf, s); };
    const auto keyMap = [&](const Value* keys) {
      if (!keys || !keys->isObject()) { why = "opRegistry: an op without a keys map"; return false; }
      for (const Value& f : keys->items)
        if (!spec(f)) return false;
      return true;
    };
    // Core copies model values recursively, so it never walks a plan without its depth cap.
    bool ok = caps.bytes != 0 && caps.depth != 0 && caps.nodes != 0;
    if (!ok) why = "opRegistry: limits.json (the §1 JSON caps) is missing";
    for (const Entry& e : entries) {
      ok = ok && keyMap(e.keys);
      if (const Value* rules = e.raw->get("rules"))
        for (const Value& r : rules->items)
          if (ok && !(r.isString() && r.text == "cropAspectFold")) { why = "opRegistry: unknown native rule"; ok = false; }
    }
    ok = ok && keyMap(askSchema().get("keys")) && spec(envelope("actions")) && spec(envelope("variants"));
    if (const Value* regexes = registry.get("regexes"); ok && regexes) {
      for (std::size_t i = 0; i < regexes->keys.size(); ++i) {
        const std::string& name = regexes->keys[i];
        if (name == "describe" || name == "note") continue;
        const GrammarRow* row = grammarNamed(name);
        if (!row || !regexes->items[i].isString() || regexes->items[i].text != row->source) {
          why = "opRegistry: regex " + name + " no longer matches core/opplan/planGrammars";
          ok = false;
        }
      }
    }
    for (const SurfaceRule& r : surfaceRules) {
      bool known = false;
      for (std::string_view k : SURFACE_RULES) known = known || r.rule == k;
      const Value* message = r.row->get("message");
      const Value* key = r.row->get("key");
      if (!known || !message || !message->isString() || !key || !key->isString() ||
          (r.rule == "anyOf" && !strings(r.row->get("keys"))) ||
          (r.rule == "pathExtension" && !strings(r.row->get("extensions")))) {
        why = "opRegistry: bad surface rule \"" + r.rule + "\"";
        ok = false;
      }
    }
    if (!ok) error = why;
    return ok;
  }

  const std::string& Schema::entriesJson() const {
    if (!entriesText.empty() || !error.empty()) return entriesText;
    Value doc = Value::object();
    doc.set("surface", Value::str(surface));
    doc.set("profile", Value::str(profile));
    Value list = Value::array();
    for (const Entry& e : entries) {
      Value row = Value::object();
      row.set("name", Value::str(e.name));
      const Value* id = e.raw->get("id");
      row.set("id", id ? *id : Value());
      row.set("bullet", e.bullet ? *e.bullet : Value());
      row.set("addendum", e.addendum ? *e.addendum : Value());
      row.set("flags", e.flags);
      const Value* requires_ = e.raw->get("requires");
      row.set("requires", requires_ ? *requires_ : Value::array());
      row.set("keys", *e.keys);
      list.items.push_back(std::move(row));
    }
    doc.set("entries", std::move(list));
    Value names = Value::array();
    for (const std::string& f : forbidden) names.items.push_back(Value::str(f));
    doc.set("forbidden", std::move(names));
    doc.set("hardFail", Value::boolean(hardFail));
    const Value* limits = registry.get("limits");
    doc.set("limits", limits ? *limits : Value::object());
    doc.set("defaultCustomLabel", Value::str(defaultCustomLabel));
    doc.set("registryBytes", Value::num(static_cast<double>(registryBytes)));
    char hex[17];
    std::snprintf(hex, sizeof hex, "%016llx", registryFnv1a64);
    doc.set("registryFnv1a64", Value::str(hex));
    entriesText = json::toJson(doc);
    return entriesText;
  }

}  // namespace stencil::core::opplan
