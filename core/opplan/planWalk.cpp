#include "planWalk.hpp"

#include "jsNumber.hpp"
#include "jsText.hpp"
#include "jsonReader.hpp"
#include "planFields.hpp"
#include "planRules.hpp"
#include "planWalker.hpp"

#include <optional>

namespace stencil::core::opplan {

  using json::Value;

  namespace {
    // §1 extraction tolerance: every ``` fence and its [a-zA-Z]* language tag go.
    std::string stripFences(std::string_view text) {
      std::string out;
      out.reserve(text.size());
      for (std::size_t i = 0; i < text.size();) {
        if (text.compare(i, 3, "```") == 0) {
          i += 3;
          while (i < text.size() && ((text[i] >= 'a' && text[i] <= 'z') || (text[i] >= 'A' && text[i] <= 'Z'))) ++i;
          continue;
        }
        out.push_back(text[i++]);
      }
      return out;
    }

    // The first balanced { … } object (string-aware), as bytes of `text`.
    std::optional<std::string_view> firstJsonObject(std::string_view text) {
      const std::size_t start = text.find('{');
      if (start == std::string_view::npos) return std::nullopt;
      std::size_t depth = 0;
      bool inStr = false, esc = false;
      for (std::size_t i = start; i < text.size(); ++i) {
        const char c = text[i];
        if (inStr) {
          if (esc) esc = false;
          else if (c == '\\') esc = true;
          else if (c == '"') inStr = false;
          continue;
        }
        if (c == '"') inStr = true;
        else if (c == '{') ++depth;
        else if (c == '}' && --depth == 0) return text.substr(start, i + 1 - start);
      }
      return std::nullopt;
    }

    Result chatOnly(std::string_view raw) {
      return resultOf(Status::CHAT_ONLY, std::string(json::jsTrim(raw)));
    }
  }  // namespace

  Result walkPlan(const Schema& s, std::string_view text) {
    std::string decoded;
    if (!json::isUtf8(text)) {
      decoded = json::decodeUtf8(text);
      text = decoded;
    }
    const std::string stripped = stripFences(text);
    const std::optional<std::string_view> candidate = firstJsonObject(stripped);
    if (!candidate) return chatOnly(text);
    json::ReadResult read = json::readJson(*candidate, s.caps);
    if (read.status == json::ReadStatus::SYNTAX) return chatOnly(text);
    if (read.status == json::ReadStatus::LIMIT) {
      const bool bytes = read.cap == json::CapHit::BYTES, depth = read.cap == json::CapHit::DEPTH;
      const double max = static_cast<double>(bytes ? s.caps.bytes : depth ? s.caps.depth : s.caps.nodes);
      const std::string n = json::jsNumberToString(max);
      const std::string detail = bytes ? "the plan's JSON is larger than " + n + " bytes"
                                 : depth ? "the plan's JSON nests deeper than " + n + " levels"
                                         : "the plan's JSON holds more than " + n + " values";
      const char* limit = bytes ? "MAX_BYTES" : depth ? "MAX_DEPTH" : "MAX_NODES";
      return resultOf(Status::INVALID, "", Value::array(), Value::array(), Value(), Value::array(),
                      errorOf("E_JSON_LIMIT", fieldsOf({{"limit", Value::str(limit)}, {"max", Value::num(max)}}), detail,
                              "Invalid plan: " + detail));
    }
    const Value& obj = read.value;
    Walker w(s);
    const Value* reply = obj.get("reply");
    const bool omitted = !reply || !reply->isString() || json::jsTrim(reply->text).empty();
    Value actions = Value::array(), variants = Value::array(), ask;
    const Entry* misplaced = nullptr;
    std::string reason;
    if (!w.actions(obj.get("actions"), actions, nullptr, &misplaced, &reason) || !w.variants(obj.get("variants"), variants) ||
        !w.ask(obj.get("ask"), ask))
      return resultOf(Status::INVALID, "", Value::array(), Value::array(), Value(), Value::array(), std::move(*w.error));
    std::string said = omitted ? "The model returned an empty plan — nothing was changed." : reply->text;
    if (omitted && (!actions.items.empty() || !variants.items.empty() || !ask.isNull())) {
      said = "Done.";
      w.warnings.items.push_back(warningOf("W_REPLY_OMITTED", Value::object(), "The model omitted its reply — the plan still ran"));
    }
    return resultOf(Status::VALID, std::move(said), std::move(actions), std::move(variants), std::move(ask),
                    std::move(w.warnings));
  }

}  // namespace stencil::core::opplan
