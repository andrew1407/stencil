#pragma once
#include "models.hpp"
#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

// Flat encoding of a Lines snapshot for the extern "C" ABIs, in two caller-owned buffers.
//   nums: [lineCount, then per line: pointCount, thickness, pointSize, locked, hidden,
//          byte lengths of color/style/fillColor/pointColor/name, then x0,y0,x1,y1,...]
//   text: those five strings per line, concatenated UTF-8, in that field order.
// Twin: browser/js/core/line/linesCodec.js.
namespace stencil::core::abi {

  // Buffer lengths (in doubles / bytes) that encodeLines needs for `lines`.
  struct LinesSize {
    int nums = 1;
    int text = 0;
  };

  // Doubles before a line's points: count, thickness, pointSize, locked, hidden, five lengths.
  inline constexpr int HEADER = 10;

  inline LinesSize linesSize(const Lines& lines) {
    LinesSize s;
    for (const Line& l : lines) {
      s.nums += HEADER + 2 * static_cast<int>(l.points.size());
      s.text += static_cast<int>(l.color.size() + l.style.size() + l.fillColor.size() +
                                 l.pointColor.size() + l.name.size());
    }
    return s;
  }

  inline void encodeLines(const Lines& lines, double* nums, std::uint8_t* text) {
    if (nums == nullptr) return;
    int i = 0, t = 0;
    nums[i++] = static_cast<double>(lines.size());
    for (const Line& l : lines) {
      nums[i++] = static_cast<double>(l.points.size());
      nums[i++] = l.thickness;
      nums[i++] = l.pointSize;
      nums[i++] = l.locked ? 1.0 : 0.0;
      nums[i++] = l.hidden ? 1.0 : 0.0;
      for (const std::string* s : {&l.color, &l.style, &l.fillColor, &l.pointColor, &l.name})
        nums[i++] = static_cast<double>(s->size());
      for (const Point& p : l.points) {
        nums[i++] = p.x;
        nums[i++] = p.y;
      }
      for (const std::string* s : {&l.color, &l.style, &l.fillColor, &l.pointColor, &l.name}) {
        if (text != nullptr && !s->empty()) {
          for (std::size_t k = 0; k < s->size(); ++k)
            text[t + k] = static_cast<std::uint8_t>((*s)[k]);
        }
        t += static_cast<int>(s->size());
      }
    }
  }

  // Layout caps, twins of LIMITS.layoutLinesMax / layoutLinePointsMax / layoutPointsMax in
  // common/config/constants.json: 64 history snapshots of an uncapped layout exhaust memory.
  inline constexpr int MAX_LAYOUT_LINES = 50000;
  inline constexpr int MAX_LINE_POINTS = 100000;
  inline constexpr int MAX_LAYOUT_POINTS = 1000000;

  // Decode what the buffers actually hold. Lengths are honoured, never trusted: a truncated or
  // malformed snapshot stops at the last complete line. The caps cut as layout.js sanitizeLines
  // does: the line that spends the last point is cut there, and every line after it dropped.
  inline Lines decodeLines(const double* nums, int numsLen, const std::uint8_t* text,
                           int textLen) {
    Lines out;
    if (nums == nullptr || numsLen < 1 || !(nums[0] > 0.0)) return out;
    // Every count is range-checked as a double first: casting NaN or 1e300 to int is UB.
    const int lineCount =
        nums[0] < MAX_LAYOUT_LINES ? static_cast<int>(nums[0]) : MAX_LAYOUT_LINES;
    int i = 1, t = 0, budget = MAX_LAYOUT_POINTS;
    for (int li = 0; li < lineCount && budget > 0; ++li) {
      if (i + HEADER > numsLen) break;
      const double declared = nums[i];
      const double thickness = nums[i + 1], pointSize = nums[i + 2];
      const bool locked = nums[i + 3] != 0.0, hidden = nums[i + 4] != 0.0;
      int len[5] = {0, 0, 0, 0, 0};
      bool ok = true;
      for (int f = 0; f < 5; ++f) {
        const double l = nums[i + 5 + f];
        ok = ok && l >= 0.0 && l <= textLen - t;
        len[f] = ok ? static_cast<int>(l) : 0;
      }
      i += HEADER;
      if (!(declared >= 0.0) || i + 2.0 * declared > numsLen) break;
      const int ptCount = static_cast<int>(declared);
      const int kept = std::min({ptCount, MAX_LINE_POINTS, budget});
      Line line;
      line.thickness = thickness;
      line.pointSize = pointSize;
      line.locked = locked;
      line.hidden = hidden;
      line.points.reserve(static_cast<std::size_t>(kept));
      for (int p = 0; p < kept; ++p)
        line.points.push_back(Point{nums[i + 2 * p], nums[i + 2 * p + 1]});
      i += 2 * ptCount;
      std::string* field[5] = {&line.color, &line.style, &line.fillColor, &line.pointColor,
                               &line.name};
      for (int f = 0; f < 5 && ok; ++f) {
        if (text == nullptr || t + len[f] > textLen) { ok = false; break; }
        field[f]->assign(reinterpret_cast<const char*>(text) + t,
                         static_cast<std::size_t>(len[f]));
        t += len[f];
      }
      if (!ok) break;
      budget -= kept;
      out.push_back(std::move(line));
    }
    return out;
  }

}
