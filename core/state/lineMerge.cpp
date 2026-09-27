#include "lineMerge.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_set>

namespace stencil::core {

  namespace {

    // A number's exact bits, -0 folded into 0 and every NaN into one: the same equality JS
    // String(n) keys by, without pulling printf's float formatting into the wasm build.
    void appendNumber(std::string& k, double v) {
      if (v == 0) v = 0;
      std::uint64_t bits = 0x7ff8000000000000ULL;
      if (!std::isnan(v)) std::memcpy(&bits, &v, sizeof bits);
      static const char hex[] = "0123456789abcdef";
      for (int shift = 60; shift >= 0; shift -= 4) k += hex[(bits >> shift) & 0xF];
    }

  }  // namespace

  std::string lineDedupeKey(const Line& line) {
    std::string k;
    k.reserve(64 + 16 * line.points.size());
    k += line.color;
    k += '|';
    k += line.pointColor;
    k += '|';
    appendNumber(k, line.thickness);
    k += '|';
    appendNumber(k, line.pointSize);
    k += '|';
    k += line.style;
    k += line.locked ? "|1|" : "|0|";
    k += line.fillColor;
    k += '|';
    for (std::size_t i = 0; i < line.points.size(); ++i) {
      if (i > 0) k += ';';
      appendNumber(k, line.points[i].x);
      k += ',';
      appendNumber(k, line.points[i].y);
    }
    return k;
  }

  std::vector<bool> mergeKeep(const Lines& server, const Lines& local) {
    std::unordered_set<std::string> seen;
    seen.reserve(server.size() + local.size());
    for (const Line& l : server) seen.insert(lineDedupeKey(l));
    std::vector<bool> keep(local.size(), false);
    for (std::size_t i = 0; i < local.size(); ++i) keep[i] = seen.insert(lineDedupeKey(local[i])).second;
    return keep;
  }

  Lines mergeLines(const Lines& server, const Lines& local) {
    const std::vector<bool> keep = mergeKeep(server, local);
    Lines out = server;
    for (std::size_t i = 0; i < local.size(); ++i)
      if (keep[i]) out.push_back(local[i]);
    return out;
  }

}
