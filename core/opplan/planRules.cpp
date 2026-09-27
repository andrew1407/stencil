#include "planRules.hpp"

#include "colorNames.hpp"
#include "planPath.hpp"
#include "text.hpp"

namespace stencil::core::opplan {

  using json::Value;

  namespace {
    bool listHas(const Value* list, std::string_view s) {
      if (!list) return false;
      for (const Value& x : list->items)
        if (x.isString() && x.text == s) return true;
      return false;
    }

    // A path's extension, unless its last dot sits in a directory name.
    bool extensionOf(std::string_view path, std::string_view* ext) {
      const std::size_t dot = path.rfind('.');
      const std::size_t slash = path.find_last_of("/\\");
      if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) return false;
      *ext = path.substr(dot + 1);
      return true;
    }

    bool passes(const std::string& rule, const Value& row, const Value* v) {
      if (rule == "anyOf") {
        const Value* keys = row.get("keys");
        if (v && keys)
          for (const Value& k : keys->items)
            if (k.isString() && v->present(k.text)) return true;
        return false;
      }
      if (rule == "knownColor")
        return !v || !v->isString() || (!v->text.empty() && v->text[0] == '#') || parseColor(v->text).has_value();
      const std::string path = v ? jsString(*v) : "undefined";
      std::string_view ext;
      return extensionOf(path, &ext) && listHas(row.get("extensions"), toLowerAscii(ext));
    }
  }  // namespace

  Why cropAspectFold(Value& a) {
    const Value* aspect = a.get("aspect");
    const Value* spec = a.get("spec");
    if (!aspect || aspect->isNull() || !spec || !spec->isObject()) return std::nullopt;
    Value folded = *spec;
    const Value* inner = folded.get("aspect");
    if (inner && !inner->isNull() && !json::strictEquals(*inner, *aspect))
      return "\"aspect\" appears both beside \"spec\" and inside it with different values";
    if (!inner || inner->isNull()) folded.set("aspect", *aspect);
    Value out = Value::object();
    for (std::size_t i = 0; i < a.keys.size(); ++i) {
      if (a.keys[i] == "aspect") continue;
      out.set(a.keys[i], a.keys[i] == "spec" ? folded : a.items[i]);
    }
    a = std::move(out);
    return std::nullopt;
  }

  Why surfaceRules(const Schema& s, const std::string& op, const Value& out) {
    for (const SurfaceRule& r : s.surfaceRules) {
      if (r.op != op) continue;
      const Value* key = r.row->get("key");
      const Value* v = key && key->isString() ? out.get(key->text) : nullptr;
      if (passes(r.rule, *r.row, v)) continue;
      const Value* message = r.row->get("message");
      std::string text = message && message->isString() ? message->text : "";
      const std::size_t at = text.find("{value}");
      if (at != std::string::npos) text.replace(at, 7, v ? jsString(*v) : "undefined");
      return text;
    }
    return std::nullopt;
  }

}  // namespace stencil::core::opplan
