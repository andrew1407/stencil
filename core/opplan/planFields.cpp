#include "planFields.hpp"

#include "jsNumber.hpp"
#include "jsText.hpp"

#include <algorithm>
#include <vector>

namespace stencil::core::opplan {

  using json::Value;

  namespace {
    std::string quotedKeys(const Value& group, const char* sep) {
      std::string out;
      for (std::size_t i = 0; i < group.items.size(); ++i)
        out += (i ? sep : "") + ("\"" + jsString(group.items[i]) + "\"");
      return out;
    }

    std::size_t countPresent(const Value& obj, const Value& group) {
      std::size_t n = 0;
      for (const Value& k : group.items) n += k.isString() && obj.present(k.text) ? 1 : 0;
      return n;
    }

    Why forms(const Value& obj, const Value& fields, const Value& list) {
      std::vector<std::string> mentioned, given;
      for (const Value& f : list.items)
        for (const Value& k : f.items) mentioned.push_back(k.text);
      for (const std::string& k : fields.keys)
        if (std::find(mentioned.begin(), mentioned.end(), k) != mentioned.end() && obj.present(k)) given.push_back(k);
      std::size_t matched = 0;
      for (const Value& f : list.items) {
        bool all = f.items.size() == given.size();
        for (const Value& k : f.items) all = all && std::find(given.begin(), given.end(), k.text) != given.end();
        matched += all ? 1 : 0;
      }
      if (matched == 1) return std::nullopt;
      std::string wording;
      for (std::size_t i = 0; i < list.items.size(); ++i) wording += (i ? " / " : "") + quotedKeys(list.items[i], "+");
      return "exactly one of " + wording + " is required";
    }

    Value pick(const Value& v, const Value& spec) {
      const Value* type = spec.get("type");
      const std::string_view t = type ? std::string_view(type->text) : std::string_view();
      if (t == "object" && truthy(spec.get("fields")) && v.isObject()) return pickFields(v, *spec.get("fields"));
      if (t == "array" && v.isArray()) {
        const Value* items = spec.get("items");
        if (!truthy(items)) return v;
        Value out = Value::array();
        for (const Value& x : v.items) out.items.push_back(pick(x, *items));
        return out;
      }
      if (t == "string" && truthy(spec.get("trim")) && v.isString()) return Value::str(std::string(json::jsTrim(v.text)));
      return v;
    }
  }  // namespace

  Why checkFields(const Schema& sc, const Value& obj, const Value& fields, const Value& holder,
                  const Path* path, std::string_view skip) {
    if (!truthy(holder.get("allowUnknown"))) {
      for (const std::string& k : obj.keys)
        if (!(!skip.empty() && k == skip) && !fields.get(k))
          return "unknown field \"" + k + "\"" + (path ? " in " + where(*path) : "");
    }
    if (const Value* list = holder.get("forms"); truthy(list))
      if (Why why = forms(obj, fields, *list)) return why;
    if (const Value* groups = holder.get("together"); truthy(groups))
      for (const Value& g : groups->items) {
        const std::size_t n = countPresent(obj, g);
        if (n && n != g.items.size()) return quotedKeys(g, " and ") + " ride together";
      }
    if (const Value* groups = holder.get("exclusive"); truthy(groups))
      for (const Value& g : groups->items)
        if (countPresent(obj, g) > 1) return "carries both " + quotedKeys(g, " and ") + " — at most one of them";
    if (const Value* minFields = holder.get("minFields"); minFields && !minFields->isNull()) {
      std::size_t n = 0;
      std::string names;
      for (std::size_t i = 0; i < fields.keys.size(); ++i) {
        n += obj.present(fields.keys[i]) ? 1 : 0;
        names += (i ? "/" : "") + fields.keys[i];
      }
      if (static_cast<double>(n) < minFields->number)
        return "needs at least " + (minFields->number == 1 ? std::string("one") : jsString(*minFields)) + " of " + names;
    }
    for (std::size_t i = 0; i < fields.keys.size(); ++i) {
      const std::string& k = fields.keys[i];
      const Value& spec = fields.items[i];
      const Path at = child(path, k);
      if (!obj.present(k)) {
        if (truthy(spec.get("required"))) return label(at) + " is required";
        if (const Value* deps = spec.get("requiredWith"))
          for (std::size_t d = 0; d < deps->keys.size(); ++d)
            if (includes(&deps->items[d], obj.get(deps->keys[d])))
              return label(at) + " is required with \"" + deps->keys[d] + "\" " + quoteOne(*obj.get(deps->keys[d]));
        continue;
      }
      if (const Value* deps = spec.get("onlyWith"))
        for (std::size_t d = 0; d < deps->keys.size(); ++d)
          if (!includes(&deps->items[d], obj.get(deps->keys[d])))
            return label(at) + " only applies with \"" + deps->keys[d] + "\" " + quotedKeys(deps->items[d], " or ");
      if (Why why = checkValue(sc, *obj.get(k), spec, at, &obj)) return why;
    }
    return std::nullopt;
  }

  Value pickFields(const Value& obj, const Value& fields) {
    Value out = Value::object();
    for (std::size_t i = 0; i < fields.keys.size(); ++i) {
      const std::string& k = fields.keys[i];
      if (obj.present(k)) out.set(k, pick(*obj.get(k), fields.items[i]));
      else if (const Value* d = fields.items[i].get("default")) out.set(k, *d);
    }
    return out;
  }

}  // namespace stencil::core::opplan
