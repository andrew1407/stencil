#include "cropSpec.hpp"

#include "lengthTokens.hpp"
#include "text.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace stencil::core {

  CropSpec parseCropSpec(const std::string& spec) {
    CropSpec out;

    // Tokenize into atoms split on whitespace/commas, with '=' as its own atom so
    // "x1=90", "x1 = 90" and "x1= 90" all reduce to ["x1", "=", "90"].
    std::vector<std::string> atoms;
    std::string cur;
    auto flush = [&]() { if (!cur.empty()) { atoms.push_back(cur); cur.clear(); } };
    for (char c : spec) {
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',') {
        flush();
      } else if (c == '=') {
        flush();
        atoms.emplace_back("=");
      } else {
        cur.push_back(c);
      }
    }
    flush();

    auto assign = [&](const std::string& key, const std::string& val) -> bool {
      const std::string k = toLowerAscii(key);
      if (k == "x1") out.x1 = val;
      else if (k == "x2") out.x2 = val;
      else if (k == "y1") out.y1 = val;
      else if (k == "y2") out.y2 = val;
      else return false;
      return true;
    };

    std::size_t i = 0;
    while (i < atoms.size()) {
      const std::string key = atoms[i++];
      if (i >= atoms.size() || atoms[i] != "=") { out.valid = false; break; }
      ++i;  // consume '='
      if (i >= atoms.size() || atoms[i] == "=") { out.valid = false; break; }
      const std::string val = atoms[i++];
      if (!assign(key, val)) { out.valid = false; break; }
    }
    return out;
  }

  std::optional<CropRect> resolveCropRect(const CropSpec& spec,
                                          const CropResolveParams& p, bool album) {
    bool ok = true;
    auto edge = [&](const std::optional<std::string>& tok, double cur,
                    double lengthPx, double pxPerCm) -> double {
      if (!tok) return cur;
      const auto r = resolveAxisPx(*tok, lengthPx, pxPerCm, cur);
      if (!r) { ok = false; return cur; }
      return *r;
    };

    // Defaults mirror the browser: edges default to the current crop, which for the
    // headless pipeline is the full image (x:0..W, y:0..H).
    double x1 = edge(spec.x1, 0.0, p.imageW, p.pxPerCmX);
    double x2 = edge(spec.x2, p.imageW, p.imageW, p.pxPerCmX);
    double y1 = edge(spec.y1, 0.0, p.imageH, p.pxPerCmY);
    double y2 = edge(spec.y2, p.imageH, p.imageH, p.pxPerCmY);
    if (!ok) return std::nullopt;

    const bool xGiven = spec.x1.has_value() || spec.x2.has_value();
    const bool yGiven = spec.y1.has_value() || spec.y2.has_value();
    if (xGiven != yGiven) {
      double aspect = cropAspect(p.pageWidth, p.pageHeight, album);  // width / height
      if (aspect <= 0.0) aspect = 1.0;
      if (xGiven) {            // have a width -> derive the height
        y1 = 0.0;
        y2 = std::abs(x2 - x1) / aspect;
      } else {                 // have a height -> derive the width
        x1 = 0.0;
        x2 = std::abs(y2 - y1) * aspect;
      }
    }

    CropRect rect;
    rect.x = std::min(x1, x2);
    rect.y = std::min(y1, y2);
    rect.width = std::abs(x2 - x1);
    rect.height = std::abs(y2 - y1);
    return rect;
  }

}  // namespace stencil::core
