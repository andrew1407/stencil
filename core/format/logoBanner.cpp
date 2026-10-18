#include "logoBanner.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace stencil::core {

  namespace {
    struct Pt {
      int col;
      int row;
    };

    // The favicon vertices snapped into the 14×6 frame: the bars land ON a row and the joins
    // step one row at a time, so the S reads square in cells that are ~2:1 tall.
    const std::vector<Pt> VERTS_S = {{11, 0}, {3, 0}, {1, 1}, {3, 2}, {10, 2}, {12, 3}, {11, 5}, {2, 5}};
    // The pressed frame's S in 10×5: hand-snapped, not scaled, so the three bars still stagger.
    const std::vector<Pt> VERTS_S_SMALL = {{8, 0}, {2, 0}, {1, 1}, {2, 2}, {7, 2}, {8, 3}, {7, 4}, {1, 4}};
    const std::vector<Pt> VERTS_P = {{3, 5}, {3, 0}, {10, 0}, {12, 1}, {10, 2}, {3, 2}};
    const std::vector<Pt> VERTS_P_SMALL = {{2, 4}, {2, 0}, {7, 0}, {8, 1}, {7, 2}, {2, 2}};
    const std::vector<Pt> VERTS_M = {{2, 5}, {2, 0}, {6, 3}, {7, 3}, {11, 0}, {11, 5}};
    const std::vector<Pt> VERTS_M_SMALL = {{1, 4}, {1, 0}, {4, 2}, {5, 2}, {8, 0}, {8, 4}};

    enum class Glyph : unsigned char { SPACE, UP, DOWN, MARK, FLAT, VERT };

    const char* glyphText(Glyph g) {
      switch (g) {
        case Glyph::UP: return "╱";
        case Glyph::DOWN: return "╲";
        case Glyph::FLAT: return "─";
        case Glyph::VERT: return "│";
        case Glyph::MARK: return "●";
        default: return " ";
      }
    }

    const std::vector<Pt>& vertsOf(LogoMark mark, bool compact) {
      switch (mark) {
        case LogoMark::P: return compact ? VERTS_P_SMALL : VERTS_P;
        case LogoMark::M: return compact ? VERTS_M_SMALL : VERTS_M;
        default: return compact ? VERTS_S_SMALL : VERTS_S;
      }
    }

    // Straight strokes between vertices (the glyph follows the segment's real slope: the S
    // runs right-to-left across its bars, so a down-left join is ╱), a ● on each vertex.
    std::vector<std::vector<Glyph>> rasterise(const BannerLayout& l, const std::vector<Pt>& vs) {
      std::vector<std::vector<Glyph>> g(l.frameHeight, std::vector<Glyph>(l.frameWidth, Glyph::SPACE));
      for (std::size_t s = 0; s + 1 < vs.size(); ++s) {
        const Pt a = vs[s];
        const Pt z = vs[s + 1];
        const bool downRight = (z.row > a.row) == (z.col > a.col);
        const Glyph stroke = z.row == a.row ? Glyph::FLAT
            : z.col == a.col              ? Glyph::VERT
            : downRight                   ? Glyph::DOWN
                                          : Glyph::UP;
        const int dc = z.col - a.col;
        const int dr = z.row - a.row;
        const int steps = std::max(std::abs(dc), std::abs(dr));
        for (int i = 1; i < steps; ++i) {
          const double t = static_cast<double>(i) / steps;
          const int cc = static_cast<int>(std::round(a.col + dc * t));
          const int rr = static_cast<int>(std::round(a.row + dr * t));
          if (g[rr][cc] == Glyph::SPACE) g[rr][cc] = stroke;
        }
      }
      for (const Pt& v : vs) g[v.row][v.col] = Glyph::MARK;
      return g;
    }

    std::string repeat(const char* glyph, int n) {
      std::string out;
      for (int i = 0; i < n; ++i) out += glyph;
      return out;
    }
  }  // namespace

  BannerLayout logoBannerLayout(bool compact) {
    BannerLayout l;
    l.indent = compact ? 4 : 2;
    l.frameWidth = compact ? 10 : 14;
    l.frameHeight = compact ? 5 : 6;
    l.margin = 1;
    l.panelWidth = l.frameWidth + 2 * l.margin;
    l.curveRows = compact ? 0 : 1;
    l.labelRow = l.frameHeight / 2;
    return l;
  }

  std::vector<std::string> rasteriseMark(LogoMark mark, bool compact) {
    const BannerLayout l = logoBannerLayout(compact);
    std::vector<std::string> rows;
    for (const auto& row : rasterise(l, vertsOf(mark, compact))) {
      std::string text;
      for (const Glyph g : row) text += glyphText(g);
      rows.push_back(text);
    }
    return rows;
  }

  std::vector<std::string> logoBannerRows(LogoMark mark, bool compact) {
    const BannerLayout l = logoBannerLayout(compact);
    const std::string indent(static_cast<std::size_t>(l.indent), ' ');
    const std::string margin(static_cast<std::size_t>(l.margin), ' ');
    const std::string curveFill(static_cast<std::size_t>(l.panelWidth), ' ');
    std::vector<std::string> rows;
    // The rounded top: an inset ╭──╮ with ╱ ╲ curving out to the full-width sides.
    rows.push_back(indent + (compact ? "" : " ") + "╭" + repeat("─", compact ? l.panelWidth : l.panelWidth - 2) + "╮");
    if (!compact) rows.push_back(indent + "╱" + curveFill + "╲");
    for (const std::string& art : rasteriseMark(mark, compact))
      rows.push_back(indent + "│" + margin + art + margin + "│");
    if (!compact) rows.push_back(indent + "╲" + curveFill + "╱");
    rows.push_back(indent + (compact ? "" : " ") + "╰" + repeat("─", compact ? l.panelWidth : l.panelWidth - 2) + "╯");
    return rows;
  }

}
