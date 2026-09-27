#include "planPath.hpp"

#include "jsNumber.hpp"

namespace stencil::core::opplan {

  using json::Kind;
  using json::Value;

  std::string where(const Path& p) {
    if (!p.key.empty()) return (p.container.empty() ? "" : p.container + ".") + p.root + p.key;
    std::string r = p.root;
    if (!r.empty() && r.back() == '.') r.pop_back();
    return r;
  }

  std::string label(const Path& p) {
    return "\"" + p.root + p.key + "\"" + (p.container.empty() ? "" : " in " + p.container);
  }

  Path child(const Path* parent, std::string_view key) {
    if (parent && !parent->key.empty()) return Path{"", std::string(key), where(*parent)};
    return Path{parent ? parent->root : "", std::string(key), ""};
  }

  Path item(const Path& p, std::size_t i) {
    return Path{p.root, p.key + "[" + std::to_string(i) + "]", p.container};
  }

  std::string jsString(const Value& v) {
    switch (v.kind) {
      case Kind::NIL: return "null";
      case Kind::BOOL: return v.flag ? "true" : "false";
      case Kind::NUMBER: return json::jsNumberToString(v.number);
      case Kind::STRING: return v.text;
      case Kind::OBJECT: return "[object Object]";
      case Kind::ARRAY: {
        std::string out;
        for (std::size_t i = 0; i < v.items.size(); ++i) {
          if (i) out += ",";
          if (!v.items[i].isNull()) out += jsString(v.items[i]);
        }
        return out;
      }
    }
    return "";
  }

  std::string quoteOne(const Value& v) { return v.isString() ? "\"" + v.text + "\"" : jsString(v); }

  std::string quoteList(const Value& list) {
    std::string out;
    for (std::size_t i = 0; i < list.items.size(); ++i) out += (i ? ", " : "") + quoteOne(list.items[i]);
    return out;
  }

  bool includes(const Value* list, const Value* v) {
    if (!list || !v) return false;
    for (const Value& x : list->items)
      if (json::strictEquals(x, *v)) return true;
    return false;
  }

}  // namespace stencil::core::opplan
