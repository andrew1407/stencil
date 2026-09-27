#include "jsonReader.hpp"

#include "jsText.hpp"
#include "jsonScan.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace stencil::core::json {

  namespace {
    // A value's weight on the caps: its values, and the deepest container level inside it.
    struct Stat {
      std::uint64_t nodes = 1;
      std::size_t depth = 0;
    };

    struct Member {
      Stat stat;
      std::size_t start = 0;
    };

    // Pass one: syntax plus the exact caps of the value JSON.parse would return. Containers past
    // the depth cap are tracked by count only — if one survives, the depth cap fires anyway.
    struct StatSink {
      struct Frame {
        bool object = false;
        std::size_t start = 0;
        std::size_t level = 0;
        Stat stat;
        std::string key;
        std::unordered_map<std::string, Member> members;
      };
      std::size_t cap = 0;
      std::vector<Frame> frames;
      std::size_t light = 0, lightStart = 0;
      std::vector<std::size_t> dead;
      Stat root;

      bool wants(std::size_t) { return true; }
      void skipped() {}
      void key(std::string k) {
        if (light == 0) frames.back().key = std::move(k);
      }
      void scalar(Value&&, std::size_t start) {
        if (light == 0) complete(Stat{}, start);
      }
      void begin(bool object, std::size_t start) {
        const std::size_t level = frames.size() + light + 1;
        if (light > 0 || (cap != 0 && level > cap)) {
          if (light++ == 0) lightStart = start;
          return;
        }
        Frame f;
        f.object = object;
        f.start = start;
        f.level = level;
        f.stat.depth = level;
        frames.push_back(std::move(f));
      }
      void end() {
        if (light > 0) {
          if (--light == 0) complete(Stat{1, frames.size() + 1}, lightStart);
          return;
        }
        Frame f = std::move(frames.back());
        frames.pop_back();
        if (f.object) {
          for (const auto& m : f.members) {
            f.stat.nodes += m.second.stat.nodes;
            f.stat.depth = std::max(f.stat.depth, m.second.stat.depth);
          }
        }
        complete(f.stat, f.start);
      }
      // A duplicate key drops the earlier value: its weight goes, and pass two skips it.
      void complete(Stat s, std::size_t start) {
        if (frames.empty()) {
          root = s;
          return;
        }
        Frame& p = frames.back();
        if (!p.object) {
          p.stat.nodes += s.nodes;
          p.stat.depth = std::max(p.stat.depth, s.depth);
          return;
        }
        const auto it = p.members.find(p.key);
        if (it == p.members.end()) {
          p.members.emplace(std::move(p.key), Member{s, start});
          return;
        }
        dead.push_back(it->second.start);
        it->second = Member{s, start};
      }
    };

    // Pass two: the tree itself, each dropped duplicate skipped unread.
    struct BuildSink {
      const std::vector<std::size_t>* dead = nullptr;
      std::vector<Value> stack;
      std::vector<std::string> keys;
      Value root;

      bool wants(std::size_t start) { return !std::binary_search(dead->begin(), dead->end(), start); }
      void skipped() { complete(Value()); }
      void key(std::string k) { keys.back() = std::move(k); }
      void scalar(Value&& v, std::size_t) { complete(std::move(v)); }
      void begin(bool object, std::size_t) {
        stack.push_back(object ? Value::object() : Value::array());
        keys.emplace_back();
      }
      void end() {
        Value v = std::move(stack.back());
        stack.pop_back();
        keys.pop_back();
        if (v.isObject()) v.finishObject();
        complete(std::move(v));
      }
      void complete(Value v) {
        if (stack.empty()) root = std::move(v);
        else if (stack.back().isObject()) stack.back().append(std::move(keys.back()), std::move(v));
        else stack.back().items.push_back(std::move(v));
      }
    };
  }  // namespace

  ReadResult readJson(std::string_view text, const Caps& caps) {
    ReadResult out;
    std::string decoded;
    if (!isUtf8(text)) {
      decoded = decodeUtf8(text);
      text = decoded;
    }
    StatSink stats;
    stats.cap = caps.depth;
    scan::Scanner first{text};
    if (!scan::drive(first, stats)) return out;
    out.status = ReadStatus::LIMIT;
    if (caps.bytes != 0 && text.size() > caps.bytes) out.cap = CapHit::BYTES;
    else if (caps.depth != 0 && stats.root.depth > caps.depth) out.cap = CapHit::DEPTH;
    else if (caps.nodes != 0 && stats.root.nodes > caps.nodes) out.cap = CapHit::NODES;
    if (out.cap != CapHit::NONE) return out;
    std::sort(stats.dead.begin(), stats.dead.end());
    BuildSink build;
    build.dead = &stats.dead;
    scan::Scanner second{text};
    scan::drive(second, build);
    out.status = ReadStatus::OK;
    out.value = std::move(build.root);
    return out;
  }

  void measure(const Value& v, std::size_t* depth, std::size_t* nodes) {
    std::size_t deepest = 0, count = 0;
    std::vector<std::pair<const Value*, std::size_t>> todo{{&v, 1}};
    while (!todo.empty()) {
      const auto [at, level] = todo.back();
      todo.pop_back();
      ++count;
      if (!at->isArray() && !at->isObject()) continue;
      deepest = std::max(deepest, level);
      for (const Value& child : at->items) todo.emplace_back(&child, level + 1);
    }
    if (depth) *depth = deepest;
    if (nodes) *nodes = count;
  }

}  // namespace stencil::core::json
