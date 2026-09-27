#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The JSON value core/opplan reads and writes, with JSON.parse's object semantics: keys iterate
// in JS Object.keys order (array indices first, ascending, then insertion order) and a
// duplicate key keeps its last value at its first position. Twin of the JS objects in
// browser/js/llm/plan/. Destruction is iterative, so a deep tree never recurses.
namespace stencil::core::json {

  enum class Kind : unsigned char { NIL, BOOL, NUMBER, STRING, ARRAY, OBJECT };

  class Value {
   public:
    Kind kind = Kind::NIL;
    bool flag = false;
    double number = 0.0;
    // A string's WTF-8 bytes, or a number's source lexeme ("" when built in code).
    std::string text;
    // An array's elements, or an object's values parallel to `keys`.
    std::vector<Value> items;
    std::vector<std::string> keys;
    // Open-addressed index into `keys` (slot = key position + 1), built past INDEX_FROM keys.
    std::vector<std::uint32_t> keyIndex;

    static constexpr std::size_t INDEX_FROM = 16;

    Value() = default;
    ~Value();
    Value(const Value&) = default;
    Value(Value&&) noexcept = default;
    Value& operator=(const Value&) = default;
    Value& operator=(Value&&) noexcept = default;

    static Value boolean(bool b);
    static Value num(double x, std::string lexeme = {});
    static Value str(std::string s);
    static Value array();
    static Value object();

    bool isNull() const { return kind == Kind::NIL; }
    bool isObject() const { return kind == Kind::OBJECT; }
    bool isArray() const { return kind == Kind::ARRAY; }
    bool isString() const { return kind == Kind::STRING; }
    bool isNumber() const { return kind == Kind::NUMBER; }
    bool isBool() const { return kind == Kind::BOOL; }

    // An own key's value, or nullptr; `present` also treats null as absent (JS `!= null`).
    const Value* get(std::string_view key) const;
    Value* get(std::string_view key);
    bool present(std::string_view key) const;

    // obj[key] = v: an existing key keeps its place, a new array index slots in ascending.
    void set(std::string key, Value v);
    // The reader's append: a duplicate overwrites in place; call finishObject() once done.
    void append(std::string key, Value v);
    void finishObject();

   private:
    std::size_t find(std::string_view key) const;
    void rebuildIndex();
  };

  // A canonical array index ("0".."4294967294"): JS orders these keys first.
  bool isArrayIndex(std::string_view key, std::uint32_t* out = nullptr);

  // `===` over two parsed values: arrays and objects are never equal.
  bool strictEquals(const Value& a, const Value& b);

}  // namespace stencil::core::json
