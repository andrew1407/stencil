#include "planResult.hpp"

#include "jsonWriter.hpp"

namespace stencil::core::opplan {

  using json::Value;

  Value fieldsOf(std::initializer_list<std::pair<const char*, Value>> members) {
    Value out = Value::object();
    for (const auto& [k, v] : members) out.set(k, v);
    return out;
  }

  Value warningOf(const char* code, const Value& fields, std::string message) {
    Value w = Value::object();
    w.set("code", Value::str(code));
    for (std::size_t i = 0; i < fields.keys.size(); ++i) w.set(fields.keys[i], fields.items[i]);
    w.set("message", Value::str(std::move(message)));
    return w;
  }

  Value errorOf(const char* code, const Value& fields, std::string detail, std::string message) {
    Value e = Value::object();
    e.set("code", Value::str(code));
    for (std::size_t i = 0; i < fields.keys.size(); ++i) e.set(fields.keys[i], fields.items[i]);
    e.set("detail", Value::str(std::move(detail)));
    e.set("message", Value::str(std::move(message)));
    return e;
  }

  Result resultOf(Status status, std::string reply, Value actions, Value variants, Value ask,
                  Value warnings, Value error) {
    static const char* const STATUS_NAMES[] = {"valid", "chatOnly", "invalid"};
    Result r;
    r.status = status;
    r.doc = Value::object();
    r.doc.set("status", Value::str(STATUS_NAMES[static_cast<int>(status)]));
    r.doc.set("reply", Value::str(std::move(reply)));
    r.doc.set("actions", std::move(actions));
    r.doc.set("variants", std::move(variants));
    r.doc.set("ask", std::move(ask));
    r.doc.set("warnings", std::move(warnings));
    r.doc.set("error", std::move(error));
    r.text = json::toJson(r.doc);
    return r;
  }

}  // namespace stencil::core::opplan
