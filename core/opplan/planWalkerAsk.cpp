#include "planFields.hpp"
#include "planWalker.hpp"

// §11: the ask card. Its structure is the registry's ask schema; each option's preview actions
// walk as nested actions, where a misplaced op costs that option its picture, never the plan.
namespace stencil::core::opplan {

  using json::Value;

  bool Walker::ask(const Value* raw, Value& out) {
    if (!raw || raw->isNull()) return true;
    const Value& schema = s.askSchema();
    const Value* keys = schema.get("keys");
    const auto askFail = [&](std::string why) { return planFail(fieldsOf({{"rule", Value::str("ask")}}), std::move(why)); };
    if (!raw->isObject()) return askFail("\"ask\" must be an object");
    const Path root{"ask.", "", ""};
    if (Why why = checkFields(s, *raw, *keys, schema, &root, "")) return askFail(*why);
    const Value card = pickFields(*raw, *keys);
    const auto field = [](const Value& o, const char* k) {
      const Value* v = o.get(k);
      return v ? *v : Value();
    };
    const Value rawOptions = field(*raw, "options");
    const Value cardOptions = field(card, "options");
    Value options = Value::array();
    for (std::size_t i = 0; i < rawOptions.items.size() && i < cardOptions.items.size(); ++i) {
      const Value& opt = rawOptions.items[i];
      const Value& shaped = cardOptions.items[i];
      const Value label = field(shaped, "label");
      Value o = Value::object();
      o.set("label", label);
      if (opt.present("actions")) {
        const Nested nested{"option", i + 1, "variants or previews"};
        Value acts = Value::array();
        const Entry* misplaced = nullptr;
        std::string reason;
        if (!actions(opt.get("actions"), acts, &nested, &misplaced, &reason)) return false;
        if (misplaced) {
          warnings.items.push_back(warningOf(
              "W_PREVIEW_DROPPED",
              fieldsOf({{"op", Value::str(misplaced->name)}, {"index", Value::num(static_cast<double>(i + 1))}, {"label", label}}),
              "Dropped the preview for ask option " + std::to_string(i + 1) + " (\"" + label.text + "\") — " + reason +
                  "; the option is still offered"));
        } else {
          o.set("actions", std::move(acts));
        }
      }
      if (opt.present("image")) o.set("image", field(shaped, "image"));
      options.items.push_back(std::move(o));
    }
    const Value* allow = card.get("allowCustom");
    const Value* custom = card.get("customLabel");
    out = Value::object();
    out.set("question", field(card, "question"));
    out.set("mode", field(card, "mode"));
    out.set("allowCustom", Value::boolean(allow && allow->isBool() && allow->flag));
    out.set("customLabel", custom && custom->isString() && !custom->text.empty() ? *custom : Value::str(s.defaultCustomLabel));
    out.set("options", std::move(options));
    return true;
  }

}  // namespace stencil::core::opplan
