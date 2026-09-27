#include "planWalker.hpp"

#include "jsNumber.hpp"
#include "jsText.hpp"
#include "planFields.hpp"
#include "planRules.hpp"

#include <cstring>

namespace stencil::core::opplan {

  using json::Value;

  namespace {
    Value num(std::size_t n) { return Value::num(static_cast<double>(n)); }

    // §1: why a registered op may not sit inside a variant or a preview.
    std::string misplacedReason(const Entry& e, const char* scope) {
      if (e.settings)
        return "editor-settings op \"" + e.name + "\" is not allowed inside " + scope +
               (e.name == "openUrl" ? " — open the URL as a top-level action; picking images off a web page is the extension assistant's job" : "");
      if (e.name == "undo" || e.name == "redo")
        return "\"" + e.name + "\" steps the live edit history — a top-level action only, not allowed inside variants or previews";
      return "\"" + e.name + "\" is a top-level action only (§2.1) — not allowed inside " + scope;
    }
  }  // namespace

  bool Walker::fail(const char* code, const Value& fields, std::string detail, std::string message) {
    error = errorOf(code, fields, std::move(detail), std::move(message));
    return false;
  }

  bool Walker::planFail(const Value& fields, std::string detail) {
    std::string message = "Invalid plan: " + detail;
    return fail("E_PLAN", fields, std::move(detail), std::move(message));
  }

  // validateAction → normalize → the surface rules; `out` gets the normalized action.
  bool Walker::accept(const Value& a, const Entry& e, Value& out) {
    const auto actionFail = [&](const std::string& why) {
      return fail("E_ACTION", fieldsOf({{"op", Value::str(e.name)}}), why, "Invalid " + e.name + " action: " + why);
    };
    const Value* rules = e.raw->get("rules");
    Value folded;
    const Value* v = &a;
    if (rules && !rules->items.empty()) {
      folded = a;
      for (const Value& rule : rules->items)
        if (rule.text == "cropAspectFold")
          if (Why why = cropAspectFold(folded)) return actionFail(*why);
      v = &folded;
    }
    if (Why why = checkFields(s, *v, *e.keys, *e.raw, nullptr, "op")) return actionFail(*why);
    Value n = Value::object();
    n.set("op", Value::str(e.name));
    Value picked = pickFields(*v, *e.keys);
    for (std::size_t i = 0; i < picked.keys.size(); ++i) n.set(picked.keys[i], std::move(picked.items[i]));
    if (Why why = surfaceRules(s, e.name, n)) return actionFail(*why);
    out.items.push_back(std::move(n));
    return true;
  }

  bool Walker::actions(const Value* list, Value& out, const Nested* nested, const Entry** misplaced,
                       std::string* reason) {
    const auto scoped = [&](const char* rule) {
      Value f = fieldsOf({{"rule", Value::str(rule)}, {"scope", Value::str(nested ? nested->scope : "actions")}});
      if (nested) f.set("index", num(nested->index));
      return f;
    };
    const std::string where = nested ? std::string(std::strcmp(nested->scope, "variant") == 0 ? "variant " : "ask option ") +
                                           std::to_string(nested->index)
                                     : "\"actions\"";
    if (!list || list->isNull()) return true;
    if (!list->isArray()) return planFail(scoped("notArray"), where + " must be an array");
    const double max = s.limitNamed("MAX_ACTIONS");
    if (static_cast<double>(list->items.size()) > max) {
      Value f = scoped("tooMany");
      f.set("max", Value::num(max));
      return planFail(f, "more than " + json::jsNumberToString(max) + " actions in " + where);
    }
    for (std::size_t item = 0; item < list->items.size(); ++item) {
      const Value& a = list->items[item];
      const Value* op = a.get("op");
      if (!a.isObject() || !op || !op->isString()) {
        Value f = scoped("notAction");
        f.set("item", num(item));
        f.set("isObject", Value::boolean(a.isObject()));
        return planFail(f, "every action in " + where + " must be an object with an \"op\"");
      }
      if (s.hardFail && s.isForbidden(op->text)) {
        std::string detail = "the \"" + op->text + "\" op is never model-drivable";
        return fail("E_FORBIDDEN", fieldsOf({{"op", *op}}), detail, "Invalid plan: " + detail);
      }
      const Entry* entry = s.find(op->text);
      if (!entry) {
        warnings.items.push_back(warningOf("W_UNKNOWN_OP", fieldsOf({{"op", *op}}), "Skipped unknown operation \"" + op->text + "\""));
        continue;
      }
      if (nested && (entry->topLevelOnly || entry->settings)) {
        *misplaced = entry;
        *reason = misplacedReason(*entry, nested->reason);
        return true;
      }
      if (!accept(a, *entry, out)) return false;
    }
    return true;
  }

  bool Walker::variants(const Value* raw, Value& out) {
    if (!raw || raw->isNull()) return true;
    if (!raw->isArray()) return planFail(fieldsOf({{"rule", Value::str("variantsNotArray")}}), "\"variants\" must be an array");
    const double max = s.limitNamed("MAX_VARIANTS");
    if (static_cast<double>(raw->items.size()) > max)
      return planFail(fieldsOf({{"rule", Value::str("tooManyVariants")}, {"max", Value::num(max)}}),
                      "more than " + json::jsNumberToString(max) + " variants");
    for (std::size_t item = 0; item < raw->items.size(); ++item) {
      const Value& v = raw->items[item];
      if (!v.isObject()) return planFail(fieldsOf({{"rule", Value::str("variantNotObject")}, {"item", num(item)}}), "every variant must be an object");
      const Value* given = v.get("label");
      const bool hasLabel = given && !given->isNull();
      if (hasLabel && !(given->isString() && static_cast<double>(json::utf16Length(given->text)) <= s.limitNamed("MAX_STRING_CHARS")))
        return planFail(fieldsOf({{"rule", Value::str("variantLabel")}, {"item", num(item)}, {"isString", Value::boolean(given->isString())}}),
                        "variant \"label\" must be a string");
      const std::size_t index = item + 1;
      const Nested nested{"variant", index, "variants"};
      Value acts = Value::array();
      const Entry* misplaced = nullptr;
      std::string reason;
      if (!actions(v.get("actions"), acts, &nested, &misplaced, &reason)) return false;
      const Value label = hasLabel ? *given : Value();
      if (misplaced) {
        const std::string shown = hasLabel && !given->text.empty() ? given->text : "variant " + std::to_string(index);
        warnings.items.push_back(warningOf("W_VARIANT_DROPPED", fieldsOf({{"op", Value::str(misplaced->name)}, {"index", num(index)}, {"label", label}}),
                                           "Dropped variant " + std::to_string(index) + " (\"" + shown + "\") — " + reason + "; the rest of the plan ran"));
        continue;
      }
      out.items.push_back(fieldsOf({{"label", label}, {"actions", std::move(acts)}}));
    }
    return true;
  }

}  // namespace stencil::core::opplan
