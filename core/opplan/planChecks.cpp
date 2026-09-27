#include "planChecks.hpp"

#include "jsNumber.hpp"
#include "jsText.hpp"
#include "planFields.hpp"
#include "planGrammars.hpp"

#include <cmath>
#include <vector>

namespace stencil::core::opplan {

  using json::Value;

  namespace {
    std::string num(double x) { return json::jsNumberToString(x); }

    bool grammarHit(const Value& name, std::string_view s) {
      const GrammarRow* row = name.isString() ? grammarNamed(name.text) : nullptr;
      return row && matches(row->grammar, s);
    }

    Why checkString(const Schema& sc, const Value& v, const Value& spec, const Path& path, const Value* parent) {
      if (!v.isString()) return label(path) + " must be a string";
      const Value* maxChars = spec.get("maxChars");
      const double max = maxChars && !maxChars->isNull() ? sc.limit(*maxChars) : sc.limitNamed("MAX_STRING_CHARS");
      if (static_cast<double>(json::utf16Length(v.text)) > max)
        return label(path) + " is longer than " + num(max) + " characters";
      const std::string_view str = truthy(spec.get("trim")) ? json::jsTrim(v.text) : std::string_view(v.text);
      const Value s = Value::str(std::string(str));
      const bool blank = json::jsTrim(str).empty();
      if (truthy(spec.get("nonEmpty")) && blank) return label(path) + " must be a non-empty string";
      const Value* choices = spec.get("enum");
      if (truthy(choices) && !includes(choices, &s)) return label(path) + " must be one of " + quoteList(*choices);
      const Value* literals = spec.get("literals");
      if (truthy(literals) && includes(literals, &s)) return std::nullopt;
      if (truthy(spec.get("blankOk")) && blank) return std::nullopt;
      std::vector<const Value*> names;
      if (const Value* by = spec.get("regexBy"); truthy(by)) {
        const Value* key = by->get("key");
        const Value* dep = parent && key && key->isString() ? parent->get(key->text) : nullptr;
        const Value* g = dep && dep->isString() && by->get("map") ? by->get("map")->get(dep->text) : nullptr;
        if (truthy(g)) names.push_back(g);
      } else if (const Value* r = spec.get("regex"); truthy(r)) {
        if (r->isArray()) {
          for (const Value& g : r->items) names.push_back(&g);
        } else {
          names.push_back(r);
        }
      }
      bool hit = names.empty();
      for (const Value* g : names) hit = hit || grammarHit(*g, str);
      if (!hit) {
        std::string wording;
        for (std::size_t i = 0; i < names.size(); ++i)
          wording += (i ? " or " : "") + sc.describe(names[i]->isString() ? names[i]->text : "");
        return label(path) + " must be " + wording;
      }
      const Value* notG = spec.get("regexNot");
      if (truthy(notG) && grammarHit(*notG, str))
        return label(path) + " must be a local value, not " + sc.describe(notG->text);
      return std::nullopt;
    }

    Why checkNumber(const Value& v, const Value& spec, const Path& path) {
      const Value* type = spec.get("type");
      const bool integer = type && type->text == "integer";
      const std::string noun = integer ? "an integer" : "a number";
      if (!v.isNumber() || !std::isfinite(v.number) || (integer && std::floor(v.number) != v.number))
        return label(path) + " must be " + noun;
      const Value* choices = spec.get("enum");
      if (truthy(choices) && !includes(choices, &v)) return label(path) + " must be one of " + quoteList(*choices);
      const Value* range = spec.get("range");
      if (!truthy(range) || range->items.size() < 2) return std::nullopt;
      const Value& lo = range->items[0];
      const Value& hi = range->items[1];
      const bool hasLo = !lo.isNull(), hasHi = !hi.isNull();
      if ((hasLo && v.number < lo.number) || (hasHi && v.number > hi.number)) {
        const std::string bounds = hasLo && hasHi ? jsString(lo) + ".." + jsString(hi)
                                   : hasLo        ? ">= " + jsString(lo)
                                                  : "<= " + jsString(hi);
        return label(path) + " must be " + noun + " " + bounds;
      }
      return std::nullopt;
    }

    Why checkBoolean(const Value& v, const Value& spec, const Path& path) {
      if (!v.isBool()) return label(path) + " must be a boolean";
      const Value* choices = spec.get("enum");
      if (truthy(choices) && !includes(choices, &v)) return label(path) + " must be " + quoteList(*choices);
      return std::nullopt;
    }

    Why checkArray(const Schema& sc, const Value& v, const Value& spec, const Path& path) {
      if (!v.isArray()) return label(path) + " must be an array";
      const Value* minV = spec.get("minItems");
      const Value* maxV = spec.get("maxItems");
      const bool hasMin = minV && !minV->isNull(), hasMax = maxV && !maxV->isNull();
      const double min = hasMin ? sc.limit(*minV) : 0, max = hasMax ? sc.limit(*maxV) : 0;
      const auto len = static_cast<double>(v.items.size());
      if (hasMin && min == 1 && len == 0) return label(path) + " must be a non-empty array";
      const bool window = hasMin && min > 1 && hasMax;
      const std::string between = label(path) + " must hold " + num(min) + ".." + num(max) + " entries";
      if (hasMax && len > max) return window ? between : "more than " + num(max) + " entries in " + label(path);
      if (hasMin && len < min) return window ? between : label(path) + " must hold at least " + num(min) + " entries";
      if (const Value* items = spec.get("items"); truthy(items))
        for (std::size_t i = 0; i < v.items.size(); ++i)
          if (Why why = checkValue(sc, v.items[i], *items, item(path, i), nullptr)) return why;
      return std::nullopt;
    }

    Why checkObject(const Schema& sc, const Value& v, const Value& spec, const Path& path) {
      if (!v.isObject()) return label(path) + " must be an object";
      const Value* fields = spec.get("fields");
      const Value* minFields = spec.get("minFields");
      if (!truthy(fields) && !(minFields && !minFields->isNull())) return std::nullopt;
      static const Value none = Value::object();
      return checkFields(sc, v, truthy(fields) ? *fields : none, spec, &path, "");
    }
  }  // namespace

  bool truthy(const Value* v) {
    if (!v) return false;
    switch (v->kind) {
      case json::Kind::NIL: return false;
      case json::Kind::BOOL: return v->flag;
      case json::Kind::NUMBER: return v->number != 0 && !std::isnan(v->number);
      case json::Kind::STRING: return !v->text.empty();
      default: return true;
    }
  }

  Why checkValue(const Schema& sc, const Value& v, const Value& spec, const Path& path, const Value* parent) {
    const Value* type = spec.get("type");
    const std::string_view t = type && type->isString() ? std::string_view(type->text) : std::string_view();
    if (t == "string") return checkString(sc, v, spec, path, parent);
    if (t == "integer" || t == "number") return checkNumber(v, spec, path);
    if (t == "boolean") return checkBoolean(v, spec, path);
    if (t == "array") return checkArray(sc, v, spec, path);
    if (t == "object") return checkObject(sc, v, spec, path);
    return "opRegistry: unknown type";
  }

}  // namespace stencil::core::opplan
